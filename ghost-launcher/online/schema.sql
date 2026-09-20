-- Ghost Arcade online leaderboard schema (Supabase / Postgres), version 2.
--
-- Paste this whole file into the Supabase dashboard: SQL Editor -> New query
-- -> Run. It is safe to run more than once, and it upgrades a version 1
-- database in place (no scores are lost).
--
-- SECURITY MODEL
-- The games ship a PUBLISHABLE key, so anyone on the internet can call this
-- API. Version 2 therefore gives that key exactly two abilities:
--
--   1. read  public.leaderboard   (a view over a function: game, mode, username, score, date)
--   2. call  public.submit_scores (a function that validates and rate-limits)
--
-- and nothing else. The key can no longer touch the scores table directly:
-- no select (so install ids are never exposed), no insert, update or delete.
-- Everything the function needs lives in the `private` schema, which the API
-- does not expose at all.
--
-- What this cannot do: stop someone submitting a made-up score under a
-- made-up name. No game that runs on the player's own computer can. The
-- limits below keep that to a nuisance you can clean up from the dashboard,
-- not something that can fill the database or reach other players' machines.
-- (The other half of that promise is in the client: ghost-sync treats every
-- byte it downloads as hostile. See online/SECURITY.md.)

-- --------------------------------------------------------------- the table

create table if not exists public.scores (
    id          bigint generated always as identity primary key,
    game        text        not null,
    mode        text        not null,
    username    text        not null,
    score       bigint      not null,
    played_on   date        not null,
    install_id  uuid        not null,
    created_at  timestamptz not null default now(),

    constraint scores_game_format      check (game ~ '^[a-z0-9-]{1,32}$'),
    constraint scores_mode_format      check (mode ~ '^[a-z0-9-]{1,24}$'),
    constraint scores_username_format  check (char_length(username) between 1 and 32
                                              and username !~ '[[:cntrl:]|]'),
    constraint scores_score_range      check (score between 1 and 100000000),
    constraint scores_date_range       check (played_on >= date '2026-01-01'
                                              and played_on <= (now() at time zone 'utc')::date + 1),
    constraint scores_unique_run unique (install_id, game, mode, username, score, played_on)
);

create index if not exists scores_board_idx on public.scores (game, mode, score desc);

-- Row Level Security on, and NO policies: every direct access by the API
-- roles is denied. (Version 1 had select + insert policies; they go.)
alter table public.scores enable row level security;
drop policy if exists "anyone can read scores" on public.scores;
drop policy if exists "anyone can submit a score" on public.scores;
revoke all on public.scores from anon, authenticated, public;

-- ------------------------------------------------------ private bookkeeping

create schema if not exists private;
revoke all on schema private from anon, authenticated, public;

-- Only these games have boards. Add a row here when a new game ships:
--   insert into private.games (slug) values ('newgame');
create table if not exists private.games (
    slug text primary key check (slug ~ '^[a-z0-9-]{1,32}$')
);
insert into private.games (slug) values
    ('blockfall'), ('coilrush'), ('brickburst'), ('skyraid'), ('ghostmaze'), ('rockdrift'),
    ('lanehop'), ('crawlshot'), ('moondrop'), ('gemdive'), ('girderclimb')
on conflict do nothing;

-- Fixed-window counters for rate limiting. `bucket` is a salted hash of the
-- caller's IP address, never the address itself.
create table if not exists private.rate_limit (
    bucket       text        not null,
    window_start timestamptz not null,
    hits         integer     not null default 0,
    primary key (bucket, window_start)
);

create table if not exists private.secrets (
    name  text primary key,
    value text not null
);
insert into private.secrets (name, value)
values ('ip_salt', encode(sha256(convert_to(gen_random_uuid()::text || clock_timestamp()::text, 'UTF8')), 'hex'))
on conflict do nothing;

revoke all on all tables in schema private from anon, authenticated, public;

-- A date that does not parse (2026-02-31) is NULL, not an error.
create or replace function private.safe_date(p text)
returns date
language plpgsql
immutable
set search_path = ''
as $$
begin
    if p is null or p !~ '^[0-9]{4}-[0-9]{2}-[0-9]{2}$' then return null; end if;
    return p::date;
exception when others then
    return null;
end;
$$;

-- Adds to a counter and returns its new value.
create or replace function private.bump(p_bucket text, p_window timestamptz, p_amount integer)
returns integer
language plpgsql
set search_path = ''
as $$
declare
    v integer;
begin
    insert into private.rate_limit as r (bucket, window_start, hits)
    values (p_bucket, p_window, p_amount)
    on conflict (bucket, window_start) do update set hits = r.hits + excluded.hits
    returning r.hits into v;
    return v;
end;
$$;

revoke all on all functions in schema private from anon, authenticated, public;

-- ------------------------------------------------------- the only way in

-- Limits (per caller IP unless it says otherwise):
--   240 calls an hour; 500 NEW rows a day; 500 rows a call;
--   5000 rows ever per install id; and the whole table stops at ~500k rows
--   so nobody can fill the free tier's disk.
-- Rows that fail validation are skipped, not errors. Being limited is not an
-- error either (an exception would roll the counters back): the reply says
-- {"limited": true}.
create or replace function public.submit_scores(p_install_id uuid, p_rows jsonb)
returns jsonb
language plpgsql
security definer
set search_path = ''
as $$
declare
    v_headers   jsonb;
    v_ip        text;
    v_bucket    text;
    v_calls     integer;
    v_today     integer;
    v_budget    integer;
    v_installed bigint;
    v_estimate  bigint;
    v_inserted  integer := 0;
begin
    if p_install_id is null or p_rows is null or jsonb_typeof(p_rows) <> 'array' then
        return jsonb_build_object('inserted', 0, 'limited', false, 'error', 'bad request');
    end if;
    if jsonb_array_length(p_rows) > 500 then
        return jsonb_build_object('inserted', 0, 'limited', false, 'error', 'too many rows in one call');
    end if;

    -- Who is calling? PostgREST hands us the request headers.
    begin
        v_headers := nullif(current_setting('request.headers', true), '')::jsonb;
    exception when others then
        v_headers := null;
    end;
    v_ip := nullif(btrim(coalesce(v_headers ->> 'cf-connecting-ip',
                                  split_part(coalesce(v_headers ->> 'x-forwarded-for', ''), ',', 1))), '');
    v_bucket := encode(sha256(convert_to(coalesce(v_ip, 'unknown')
                  || coalesce((select s.value from private.secrets s where s.name = 'ip_salt'), ''), 'UTF8')), 'hex');

    -- Housekeeping: forget counters older than two days.
    delete from private.rate_limit where window_start < now() - interval '2 days';

    v_calls := private.bump('calls:' || v_bucket, date_trunc('hour', now()), 1);
    if v_calls > 240 then
        return jsonb_build_object('inserted', 0, 'limited', true, 'reason', 'too many requests this hour');
    end if;

    select coalesce(max(r.hits), 0) into v_today
    from private.rate_limit r
    where r.bucket = 'rows:' || v_bucket and r.window_start = date_trunc('day', now());
    v_budget := 500 - v_today;
    if v_budget <= 0 then
        return jsonb_build_object('inserted', 0, 'limited', true, 'reason', 'daily limit reached');
    end if;

    select count(*) into v_installed from public.scores s where s.install_id = p_install_id;
    if v_installed >= 5000 then
        return jsonb_build_object('inserted', 0, 'limited', true, 'reason', 'this install has reached its limit');
    end if;

    select c.reltuples::bigint into v_estimate from pg_catalog.pg_class c where c.oid = 'public.scores'::regclass;
    if coalesce(v_estimate, 0) > 500000 then
        return jsonb_build_object('inserted', 0, 'limited', true, 'reason', 'the leaderboard is full');
    end if;

    with incoming as (
        select r ->> 'game' as game, r ->> 'mode' as mode, r ->> 'username' as username,
               case when (r ->> 'score') ~ '^[0-9]{1,9}$' then (r ->> 'score')::bigint end as score,
               private.safe_date(r ->> 'played_on') as played_on
        from jsonb_array_elements(p_rows) as r
        where jsonb_typeof(r) = 'object'
    ), valid as (
        select i.game, i.mode, i.username, i.score, i.played_on
        from incoming i
        where i.game is not null and i.mode is not null and i.username is not null
          and i.score is not null and i.played_on is not null
          and exists (select 1 from private.games g where g.slug = i.game)
          and i.mode ~ '^[a-z0-9-]{1,24}$'
          and char_length(i.username) between 1 and 32
          and octet_length(i.username) = char_length(i.username)   -- plain ASCII only
          and i.username !~ '[[:cntrl:]|]'
          and btrim(i.username) <> ''
          and i.score between 1 and 100000000
          and i.played_on >= date '2026-01-01'
          and i.played_on <= (now() at time zone 'utc')::date + 1
        limit v_budget
    )
    insert into public.scores (game, mode, username, score, played_on, install_id)
    select v.game, v.mode, v.username, v.score, v.played_on, p_install_id
    from valid v
    on conflict on constraint scores_unique_run do nothing;
    get diagnostics v_inserted = row_count;

    if v_inserted > 0 then
        perform private.bump('rows:' || v_bucket, date_trunc('day', now()), v_inserted);
    end if;
    return jsonb_build_object('inserted', v_inserted, 'limited', false);
end;
$$;

-- Only the anonymous API role: the games never sign anyone in.
revoke all on function public.submit_scores(uuid, jsonb) from public, authenticated;
grant execute on function public.submit_scores(uuid, jsonb) to anon;

-- ------------------------------------------------------- the public board

-- Each player's best run per game+mode, top 10, known games only.
--
-- The API roles cannot read the table, so the board is produced by a
-- SECURITY DEFINER function that returns exactly five harmless columns plus
-- the rank (never install_id or created_at), and `public.leaderboard` is an
-- ordinary security_invoker view over it. (An owner-rights view would do the
-- same job, but Supabase's advisor rightly flags those as critical, because
-- they are usually an accident. This way nothing is flagged and the window is
-- just as narrow.)
create or replace function public.leaderboard_rows()
returns table (game text, mode text, username text, score bigint, played_on date, rank bigint)
language sql
stable
security definer
set search_path = ''
as $$
    select ranked.game, ranked.mode, ranked.username, ranked.score, ranked.played_on, ranked.rank
    from (
        select best.game, best.mode, best.username, best.score, best.played_on,
               row_number() over (partition by best.game, best.mode
                                  order by best.score desc, best.created_at asc) as rank
        from (
            select distinct on (s.game, s.mode, lower(s.username))
                   s.game, s.mode, s.username, s.score, s.played_on, s.created_at
            from public.scores s
            join private.games g on g.slug = s.game
            order by s.game, s.mode, lower(s.username), s.score desc, s.created_at asc
        ) best
    ) ranked
    where ranked.rank <= 10;
$$;

revoke all on function public.leaderboard_rows() from public, authenticated;
grant execute on function public.leaderboard_rows() to anon;

drop view if exists public.leaderboard;
create view public.leaderboard
with (security_invoker = true) as
select r.game, r.mode, r.username, r.score, r.played_on, r.rank
from public.leaderboard_rows() r;

revoke all on public.leaderboard from anon, authenticated, public;
grant select on public.leaderboard to anon;

-- Supabase's own helper (installed by the dashboard's "Run and enable RLS"):
-- an event trigger that switches RLS on for new public tables. Keep it, but
-- nobody needs to be able to call it.
do $$
begin
    if to_regprocedure('public.rls_auto_enable()') is not null then
        revoke all on function public.rls_auto_enable() from public, anon, authenticated;
    end if;
end;
$$;

-- Tell the API layer about the new function straight away.
notify pgrst, 'reload schema';

-- ----------------------------------------------------------- moderation
-- Run these by hand in the SQL editor when you need them.
--   remove a name:        delete from public.scores where lower(username) = lower('bad name');
--   remove one install:   delete from public.scores where install_id = '...';
--   see who is noisy:     select install_id, count(*) from public.scores group by 1 order by 2 desc limit 20;
