-- Ghost Arcade online schema (Supabase / Postgres), version 3: the
-- leaderboard, plus Pond Kings' online rooms and rival crews.
--
-- Paste this whole file into the Supabase dashboard: SQL Editor -> New query
-- -> Run. It is safe to run more than once, and it upgrades a version 1 or 2
-- database in place (no scores are lost).
--
-- SECURITY MODEL
-- The games ship a PUBLISHABLE key, so anyone on the internet can call this
-- API. The key therefore gets exactly these abilities:
--
--   1. read  public.leaderboard   (a view over a function: game, mode, username, score, date)
--   2. call  public.submit_scores (a function that validates and rate-limits)
--   3. call  the pk_room_* and pk_crew_* functions (Pond Kings, version 3,
--      at the end of this file): validated, rate limited, and they only ever
--      hand back what the caller is entitled to
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
    ('lanehop'), ('crawlshot'), ('moondrop'), ('gemdive'), ('girderclimb'), ('pondkings')
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

-- =================================================== Pond Kings (version 3)
--
-- ONLINE ROOMS. A host opens a room with its two addresses (what its router
-- shows the internet, and its LAN address) and a random relay key, and gets a
-- six-letter code. A guest joins with the code and its own addresses and gets
-- the host's; the host polls until a guest is there, then closes the room.
-- The two games then talk to each other directly, or through the Realtime
-- broadcast channel named after the relay key. Rooms vanish after 15 minutes
-- and only ever hold addresses, a hashed secret and the relay key.
--
-- RIVAL CREWS. One crew per install, owned by a secret only that install
-- knows (the server keeps its hash), shared by an eight-letter code. A crew
-- is three kids' names, builds and signature moves plus four numbers for how
-- its owner plays, in a strict format: nothing free-form can be stored.
--
-- Limits per caller IP, an hour: 30 rooms, 60 joins, 3600 polls, 120 closes,
-- 20 crew saves, 240 crew lookups, 30 crew deletions. At most 2000 open rooms
-- and 50000 crews. Codes avoid look-alike characters (no I, O, 0 or 1).

-- The caller, as submit_scores sees it: a salted hash of the IP, never the IP.
create or replace function private.caller_bucket()
returns text
language plpgsql
stable
set search_path = ''
as $$
declare
    v_headers jsonb;
    v_ip text;
begin
    begin
        v_headers := nullif(current_setting('request.headers', true), '')::jsonb;
    exception when others then
        v_headers := null;
    end;
    v_ip := nullif(btrim(coalesce(v_headers ->> 'cf-connecting-ip',
                                  split_part(coalesce(v_headers ->> 'x-forwarded-for', ''), ',', 1))), '');
    return encode(sha256(convert_to(coalesce(v_ip, 'unknown')
             || coalesce((select s.value from private.secrets s where s.name = 'ip_salt'), ''), 'UTF8')), 'hex');
end;
$$;

-- "a.b.c.d:port" with every part in range. Checked step by step, so nothing is
-- cast before it is known to be digits.
create or replace function private.pk_addr_ok(p text)
returns boolean
language plpgsql
immutable
set search_path = ''
as $$
declare
    v_parts text[];
begin
    if p is null or p !~ '^[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}:[0-9]{1,5}$' then return false; end if;
    v_parts := regexp_split_to_array(p, '[.:]');
    if v_parts[1]::int > 255 or v_parts[2]::int > 255 or v_parts[3]::int > 255 or v_parts[4]::int > 255 then return false; end if;
    return v_parts[5]::int between 1 and 65535;
end;
$$;

-- A code of p_len (up to 12) characters from crypto-random bytes: the random
-- bytes of a v4 UUID (6 to 9 carry its version and variant, so they're skipped).
create or replace function private.pk_code(p_len int)
returns text
language plpgsql
volatile
set search_path = ''
as $$
declare
    v_alphabet constant text := 'ABCDEFGHJKLMNPQRSTUVWXYZ23456789';
    v_bytes bytea := decode(replace(gen_random_uuid()::text, '-', ''), 'hex');
    v_code text := '';
begin
    for i in 0 .. least(p_len, 12) - 1 loop
        v_code := v_code || substr(v_alphabet, 1 + (get_byte(v_bytes, case when i < 6 then i else i + 4 end) % 32), 1);
    end loop;
    return v_code;
end;
$$;

-- What a crew may say: 1;name,build,move;name,build,move;name,build,move;shoot,pass,check,side
create or replace function private.pk_crew_ok(p text)
returns boolean
language sql
immutable
set search_path = ''
as $$
    select p is not null and char_length(p) <= 200 and p ~ '^[ -~]+$'
       and p ~ '^1(;[^,;|=]{1,15},[0-2],[0-6]){3};-?[0-9]{1,3},-?[0-9]{1,3},-?[0-9]{1,3},-?[01]$';
$$;

create table if not exists private.pk_rooms (
    code        text primary key check (code ~ '^[A-HJ-NP-Z2-9]{6}$'),
    host_token  text not null check (host_token ~ '^[0-9a-f]{64}$'),
    host_addr   text not null,
    host_lan    text,
    relay       text not null check (relay ~ '^[0-9a-f]{32}$'),
    guest_addr  text,
    guest_lan   text,
    created_at  timestamptz not null default now(),
    joined_at   timestamptz
);

create table if not exists private.pk_crews (
    code        text primary key check (code ~ '^[A-HJ-NP-Z2-9]{8}$'),
    owner_token text not null unique check (owner_token ~ '^[0-9a-f]{64}$'),
    name        text not null check (char_length(name) between 1 and 23 and name ~ '^[ -~]+$' and name !~ '[,|=;]'),
    crew        text not null check (private.pk_crew_ok(crew)),
    updated_at  timestamptz not null default now()
);

alter table private.pk_rooms enable row level security;
alter table private.pk_crews enable row level security;
revoke all on all tables in schema private from anon, authenticated, public;
revoke all on all functions in schema private from anon, authenticated, public;

create or replace function public.pk_room_open(p_secret text, p_addr text, p_lan text, p_relay text)
returns jsonb
language plpgsql
security definer
set search_path = ''
as $$
declare
    v_bucket text;
    v_code text;
    v_try int := 0;
begin
    if p_secret is null or p_secret !~ '^[0-9a-f]{32,64}$' or not private.pk_addr_ok(p_addr)
       or (coalesce(p_lan, '') <> '' and not private.pk_addr_ok(p_lan))
       or p_relay is null or p_relay !~ '^[0-9a-f]{32}$' then
        return jsonb_build_object('error', 'bad request');
    end if;
    v_bucket := private.caller_bucket();
    delete from private.pk_rooms where created_at < now() - interval '15 minutes';
    delete from private.rate_limit where window_start < now() - interval '2 days';
    if private.bump('pkopen:' || v_bucket, date_trunc('hour', now()), 1) > 30 then
        return jsonb_build_object('limited', true);
    end if;
    if (select count(*) from private.pk_rooms) >= 2000 then
        return jsonb_build_object('limited', true);
    end if;
    loop
        v_code := private.pk_code(6);
        begin
            insert into private.pk_rooms (code, host_token, host_addr, host_lan, relay)
            values (v_code, encode(sha256(convert_to(p_secret, 'UTF8')), 'hex'), p_addr, nullif(p_lan, ''), p_relay);
            exit;
        exception when unique_violation then
            v_try := v_try + 1;
            if v_try > 8 then return jsonb_build_object('error', 'busy'); end if;
        end;
    end loop;
    return jsonb_build_object('code', v_code);
end;
$$;

create or replace function public.pk_room_join(p_code text, p_addr text, p_lan text)
returns jsonb
language plpgsql
security definer
set search_path = ''
as $$
declare
    v_room private.pk_rooms;
begin
    if p_code is null or upper(p_code) !~ '^[A-HJ-NP-Z2-9]{6}$' or not private.pk_addr_ok(p_addr)
       or (coalesce(p_lan, '') <> '' and not private.pk_addr_ok(p_lan)) then
        return jsonb_build_object('error', 'bad request');
    end if;
    if private.bump('pkjoin:' || private.caller_bucket(), date_trunc('hour', now()), 1) > 60 then
        return jsonb_build_object('limited', true);
    end if;
    select * into v_room from private.pk_rooms r
    where r.code = upper(p_code) and r.created_at > now() - interval '15 minutes'
    for update;
    if not found then return jsonb_build_object('error', 'no such room'); end if;
    if v_room.guest_addr is not null then return jsonb_build_object('error', 'room full'); end if;
    update private.pk_rooms set guest_addr = p_addr, guest_lan = nullif(p_lan, ''), joined_at = now()
    where code = v_room.code;
    return jsonb_build_object('host_addr', v_room.host_addr, 'host_lan', coalesce(v_room.host_lan, ''), 'relay', v_room.relay);
end;
$$;

create or replace function public.pk_room_poll(p_code text, p_secret text)
returns jsonb
language plpgsql
security definer
set search_path = ''
as $$
declare
    v_room private.pk_rooms;
begin
    if p_code is null or upper(p_code) !~ '^[A-HJ-NP-Z2-9]{6}$' or p_secret is null or p_secret !~ '^[0-9a-f]{32,64}$' then
        return jsonb_build_object('error', 'bad request');
    end if;
    if private.bump('pkpoll:' || private.caller_bucket(), date_trunc('hour', now()), 1) > 3600 then
        return jsonb_build_object('limited', true);
    end if;
    select * into v_room from private.pk_rooms r
    where r.code = upper(p_code) and r.host_token = encode(sha256(convert_to(p_secret, 'UTF8')), 'hex')
      and r.created_at > now() - interval '15 minutes';
    if not found then return jsonb_build_object('error', 'no such room'); end if;
    if v_room.guest_addr is null then return jsonb_build_object('waiting', true); end if;
    return jsonb_build_object('guest_addr', v_room.guest_addr, 'guest_lan', coalesce(v_room.guest_lan, ''));
end;
$$;

create or replace function public.pk_room_close(p_code text, p_secret text)
returns jsonb
language plpgsql
security definer
set search_path = ''
as $$
begin
    if p_code is null or upper(p_code) !~ '^[A-HJ-NP-Z2-9]{6}$' or p_secret is null or p_secret !~ '^[0-9a-f]{32,64}$' then
        return jsonb_build_object('error', 'bad request');
    end if;
    if private.bump('pkclose:' || private.caller_bucket(), date_trunc('hour', now()), 1) > 120 then
        return jsonb_build_object('limited', true);
    end if;
    delete from private.pk_rooms r
    where r.code = upper(p_code) and r.host_token = encode(sha256(convert_to(p_secret, 'UTF8')), 'hex');
    return jsonb_build_object('closed', true);
end;
$$;

create or replace function public.pk_crew_save(p_secret text, p_name text, p_crew text)
returns jsonb
language plpgsql
security definer
set search_path = ''
as $$
declare
    v_token text;
    v_code text;
    v_try int := 0;
begin
    if p_secret is null or p_secret !~ '^[0-9a-f]{32,64}$'
       or p_name is null or char_length(p_name) not between 1 and 23 or p_name !~ '^[ -~]+$' or p_name ~ '[,|=;]'
       or not private.pk_crew_ok(p_crew) then
        return jsonb_build_object('error', 'bad request');
    end if;
    if private.bump('pkcrew:' || private.caller_bucket(), date_trunc('hour', now()), 1) > 20 then
        return jsonb_build_object('limited', true);
    end if;
    v_token := encode(sha256(convert_to(p_secret, 'UTF8')), 'hex');
    update private.pk_crews set name = p_name, crew = p_crew, updated_at = now()
    where owner_token = v_token
    returning code into v_code;
    if v_code is not null then return jsonb_build_object('code', v_code); end if;
    if (select count(*) from private.pk_crews) >= 50000 then
        return jsonb_build_object('limited', true);
    end if;
    loop
        v_code := private.pk_code(8);
        begin
            insert into private.pk_crews (code, owner_token, name, crew) values (v_code, v_token, p_name, p_crew);
            exit;
        exception when unique_violation then
            v_try := v_try + 1;
            if v_try > 8 then return jsonb_build_object('error', 'busy'); end if;
        end;
    end loop;
    return jsonb_build_object('code', v_code);
end;
$$;

create or replace function public.pk_crew_get(p_code text)
returns jsonb
language plpgsql
security definer
set search_path = ''
as $$
declare
    v_crew private.pk_crews;
begin
    if p_code is null or upper(p_code) !~ '^[A-HJ-NP-Z2-9]{8}$' then
        return jsonb_build_object('error', 'bad request');
    end if;
    if private.bump('pkcrewget:' || private.caller_bucket(), date_trunc('hour', now()), 1) > 240 then
        return jsonb_build_object('limited', true);
    end if;
    select * into v_crew from private.pk_crews c where c.code = upper(p_code);
    if not found then return jsonb_build_object('error', 'no such crew'); end if;
    return jsonb_build_object('name', v_crew.name, 'crew', v_crew.crew);
end;
$$;

create or replace function public.pk_crew_delete(p_secret text)
returns jsonb
language plpgsql
security definer
set search_path = ''
as $$
begin
    if p_secret is null or p_secret !~ '^[0-9a-f]{32,64}$' then
        return jsonb_build_object('error', 'bad request');
    end if;
    if private.bump('pkcrewdel:' || private.caller_bucket(), date_trunc('hour', now()), 1) > 30 then
        return jsonb_build_object('limited', true);
    end if;
    delete from private.pk_crews c where c.owner_token = encode(sha256(convert_to(p_secret, 'UTF8')), 'hex');
    return jsonb_build_object('deleted', true);
end;
$$;

-- Only the anonymous API role, and only these.
revoke all on function public.pk_room_open(text, text, text, text) from public, authenticated;
revoke all on function public.pk_room_join(text, text, text) from public, authenticated;
revoke all on function public.pk_room_poll(text, text) from public, authenticated;
revoke all on function public.pk_room_close(text, text) from public, authenticated;
revoke all on function public.pk_crew_save(text, text, text) from public, authenticated;
revoke all on function public.pk_crew_get(text) from public, authenticated;
revoke all on function public.pk_crew_delete(text) from public, authenticated;
grant execute on function public.pk_room_open(text, text, text, text) to anon;
grant execute on function public.pk_room_join(text, text, text) to anon;
grant execute on function public.pk_room_poll(text, text) to anon;
grant execute on function public.pk_room_close(text, text) to anon;
grant execute on function public.pk_crew_save(text, text, text) to anon;
grant execute on function public.pk_crew_get(text) to anon;
grant execute on function public.pk_crew_delete(text) to anon;

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
