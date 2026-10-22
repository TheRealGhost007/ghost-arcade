/* The rival crews' text form, style and files (crews.c) and the network
 * plumbing that needs no internet (net.c: addresses, STUN answers, a
 * loopback datagram). Compiled by inclusion from test_main.c; the file
 * tests run in a throwaway XDG_DATA_HOME. */
#include "../src/crews.h"
#include "../src/net.h"
#include <time.h>

static void SleepMs(long ms) {
    struct timespec ts = {ms / 1000, (ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
}

static CrewCard SampleCrew(void) {
    CrewCard c;
    memset(&c, 0, sizeof(c));
    snprintf(c.name, sizeof(c.name), "TRG's Crew");
    const char *names[TEAM_SIZE] = {"Zippy", "Flurry", "Moose"};
    for (int k = 0; k < TEAM_SIZE; k++) {
        snprintf(c.kids[k].name, sizeof(c.kids[k].name), "%s", names[k]);
        c.kids[k].build = (uint8_t)k;
        c.kids[k].trait = (uint8_t)(k == 1 ? TRAIT_SNIPER : k == 2 ? TRAIT_WALL : TRAIT_NONE);
    }
    c.lean = (AiLean){12, -8, 25, -1};
    return c;
}

static void test_crews_text(void) {
    CrewCard c = SampleCrew(), back;
    char text[CREW_TEXT_MAX + 8];
    CHECK(Crews_Encode(&c, text, sizeof(text)) && strcmp(text, "1;Zippy,0,0;Flurry,1,2;Moose,2,6;12,-8,25,-1") == 0, "crews: a crew as one line of text");
    CHECK(Crews_Decode("TRG's Crew", text, &back) && !strcmp(back.name, "TRG's Crew") && !strcmp(back.kids[1].name, "Flurry") &&
          back.kids[2].trait == TRAIT_WALL && back.kids[0].from == JOURNEY_FRIEND && back.lean.check == 25 && back.lean.side == -1,
          "crews: and back again, every kid and habit");
    CHECK(!Crews_Decode("X", "2;Zippy,0,0;Sam,1,0;Tank,2,0;0,0,0,0", &back), "crews: another format version is refused");
    CHECK(!Crews_Decode("X", "1;Zippy,0,0;Sam,1,0;0,0,0,0", &back), "crews: two kids is not a crew");
    CHECK(!Crews_Decode("X", "1;Zippy,3,0;Sam,1,0;Tank,2,0;0,0,0,0", &back), "crews: an unknown build is refused");
    CHECK(!Crews_Decode("X", "1;Zippy,0,7;Sam,1,0;Tank,2,0;0,0,0,0", &back), "crews: an unknown move is refused");
    CHECK(!Crews_Decode("X", "1;Zippy,0,0;Sam,1,0;Tank,2,0;0,0,0,2", &back), "crews: a side that isn't one is refused");
    CHECK(!Crews_Decode("X", "1;Zippy,0,0;Sam,1,0;Tank,2,0;0,0,0,0;extra", &back), "crews: anything left over is refused");
    CHECK(!Crews_Decode("X", "1;ThisNameIsFarTooLong,0,0;Sam,1,0;Tank,2,0;0,0,0,0", &back), "crews: a name too long is refused");
    CHECK(!Crews_Decode("X", "1;,0,0;Sam,1,0;Tank,2,0;0,0,0,0", &back), "crews: an empty name is refused");
    CHECK(!Crews_Decode("", "1;Zippy,0,0;Sam,1,0;Tank,2,0;0,0,0,0", &back), "crews: a crew needs a name");
    CHECK(Crews_Decode("X", "1;Zippy,0,0;Sam,1,0;Tank,2,0;99,-99,0,1", &back) && back.lean.shoot == 30 && back.lean.pass == -30,
          "crews: habits from the server are kept within bounds");
    char name[32];
    Crews_CleanName("  Bad|Name=;With,Separators and much too long  ", name, sizeof(name));
    CHECK(strlen(name) <= 23 && !strpbrk(name, "|=;,") && name[0] == 'B', "crews: a crew name is cleaned for the server");
    CHECK(Crews_ValidCode("AB3DEF7H", 8) && !Crews_ValidCode("AB3DEF7", 8) && !Crews_ValidCode("AB3DEFIH", 8) && !Crews_ValidCode("ab3def7h", 8) &&
          !Crews_ValidCode("AB3DEF0H", 8), "crews: codes use the alphabet without look-alikes");
}

static void test_crews_style(void) {
    PlayStyle ps;
    memset(&ps, 0, sizeof(ps));
    AiLean l = Crews_LeanFrom(&ps);
    CHECK(l.shoot == 0 && l.pass == 0 && l.check == 0 && l.side == 0, "crews: no games, no habits");
    Habits sniper = {.shots = 30, .passes = 2, .checks = 0, .carryTicks = 1800, .sideShots = {22, 4, 4}, .shotDistance = 0};
    for (int k = 0; k < 4; k++) Crews_AddGame(&ps, &sniper);
    l = Crews_LeanFrom(&ps);
    CHECK(l.shoot >= 10 && l.pass <= -10 && l.check <= -10 && l.side == -1, "crews: a shooter who never passes or hits, aiming high");
    char words[96];
    Crews_Describe(&l, words, sizeof(words));
    CHECK(strstr(words, "Shoots on sight") && strstr(words, "keeps the puck") && strstr(words, "aims high"), "crews: and says so in words");
    memset(&ps, 0, sizeof(ps));
    Habits passer = {.shots = 3, .passes = 40, .checks = 12, .carryTicks = 3000, .sideShots = {1, 1, 1}, .shotDistance = 0};
    for (int k = 0; k < 3; k++) Crews_AddGame(&ps, &passer);
    l = Crews_LeanFrom(&ps);
    CHECK(l.pass >= 20 && l.shoot <= -10 && l.check >= 20 && l.side == 0, "crews: a passer who hits");
    for (int k = 0; k < 100; k++) Crews_AddGame(&ps, &passer);
    CHECK(ps.games < 40, "crews: only the recent games count for much");
    Crews_Describe(&(AiLean){0, 0, 0, 0}, words, sizeof(words));
    CHECK(!strcmp(words, "Plays it straight"), "crews: no leaning says so");
}

static void test_crews_files(void) {
    char tmpl[] = "/tmp/pondkings-crews-XXXXXX";
    char *dir = mkdtemp(tmpl);
    CHECK(dir != NULL, "crews: a throwaway data folder");
    if (!dir) return;
    char *old = getenv("XDG_DATA_HOME") ? strdup(getenv("XDG_DATA_HOME")) : NULL;
    setenv("XDG_DATA_HOME", dir, 1);

    CrewCard list[CREWS_MAX_FRIENDS + 2];
    CHECK(Crews_LoadFriends(list, CREWS_MAX_FRIENDS) == 0, "crews: no friends' crews to begin with");
    CrewCard a = SampleCrew(), b = SampleCrew();
    snprintf(a.code, sizeof(a.code), "AAAA2222");
    snprintf(b.code, sizeof(b.code), "BBBB3333");
    snprintf(b.name, sizeof(b.name), "The Others");
    CrewCard two[2] = {a, b};
    CHECK(Crews_SaveFriends(two, 2) && Crews_LoadFriends(list, CREWS_MAX_FRIENDS) == 2 && !strcmp(list[1].name, "The Others") &&
          !strcmp(list[0].code, "AAAA2222") && list[0].lean.check == 25, "crews: friends' crews are kept between games");

    char path[700];
    snprintf(path, sizeof(path), "%s/pondkings/crews.txt", dir);
    FILE *f = fopen(path, "a");
    if (f) {
        fputs("garbage line\nCCCC4444|Bad|1;x,9,9;y,0,0;z,0,0;0,0,0,0\nAAAA2222|Dup|1;Zippy,0,0;Sam,1,0;Tank,2,0;0,0,0,0\n", f);
        fclose(f);
    }
    CHECK(Crews_LoadFriends(list, CREWS_MAX_FRIENDS) == 2, "crews: broken lines and repeats in the file are skipped");

    PlayStyle ps = {5, 50, 20, 10, 4000, {10, 20, 20}}, back;
    CHECK(Crews_SaveStyle(&ps) && Crews_LoadStyle(&back) && back.games == 5 && back.carryTicks == 4000 && back.sideShots[2] == 20,
          "crews: the play style is kept between games");

    char code[16];
    Crews_LoadMyCode(code, sizeof(code));
    CHECK(code[0] == '\0', "crews: no code before sharing");
    CHECK(Crews_SaveMyCode("ZZZZ9999"), "crews: the shared code is saved");
    Crews_LoadMyCode(code, sizeof(code));
    CHECK(!strcmp(code, "ZZZZ9999"), "crews: and read back");

    char s1[80], s2[80];
    CHECK(Crews_Secret(s1, sizeof(s1)) && strlen(s1) == 64 && Crews_Secret(s2, sizeof(s2)) && !strcmp(s1, s2), "crews: one secret per install, made once");
    snprintf(path, sizeof(path), "%s/pondkings/crew-secret", dir);
    struct stat st;
    CHECK(stat(path, &st) == 0 && (st.st_mode & 077) == 0, "crews: the secret is readable by this user only");

    const char *files[] = {"crews.txt", "style.txt", "mycrew.txt", "crew-secret"};
    for (int k = 0; k < 4; k++) {
        snprintf(path, sizeof(path), "%s/pondkings/%s", dir, files[k]);
        unlink(path);
    }
    snprintf(path, sizeof(path), "%s/pondkings", dir);
    rmdir(path);
    rmdir(dir);
    if (old) { setenv("XDG_DATA_HOME", old, 1); free(old); }
    else unsetenv("XDG_DATA_HOME");
}

static void test_net_plumbing(void) {
    NetAddr a;
    char text[32];
    CHECK(Net_ParseAddr("203.0.113.5:54321", &a) && a.ip == 0xCB007105u && a.port == 54321, "net: an address from text");
    Net_AddrString(a, text, sizeof(text));
    CHECK(!strcmp(text, "203.0.113.5:54321"), "net: and back to text");
    CHECK(!Net_ParseAddr("256.0.0.1:80", &a) && !Net_ParseAddr("1.2.3.4:0", &a) && !Net_ParseAddr("1.2.3.4:65536", &a) &&
          !Net_ParseAddr("1.2.3:80", &a) && !Net_ParseAddr("1.2.3.4:80x", &a) && !Net_ParseAddr("", &a) && !Net_ParseAddr("a.b.c.d:1", &a),
          "net: nonsense addresses are refused");
    CHECK(!Net_AddrUsable((NetAddr){0, 5}) && !Net_AddrUsable((NetAddr){0x7F000001u, 0}) && !Net_AddrUsable((NetAddr){0xE0000001u, 9}) &&
          Net_AddrUsable((NetAddr){0x7F000001u, 9}), "net: nowhere and multicast are not addresses to send to");

    /* a STUN answer, built by hand: XOR-MAPPED-ADDRESS 203.0.113.5:54321 */
    uint8_t txid[STUN_TXID] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    uint8_t req[64];
    int rl = Stun_Request(req, sizeof(req), txid);
    CHECK(rl == 20 && req[1] == 1 && Stun_IsMessage(req, rl), "net: a STUN request");
    uint8_t resp[64];
    memset(resp, 0, sizeof(resp));
    resp[0] = 0x01; resp[1] = 0x01; resp[2] = 0; resp[3] = 12;
    resp[4] = 0x21; resp[5] = 0x12; resp[6] = 0xA4; resp[7] = 0x42;
    memcpy(resp + 8, txid, STUN_TXID);
    uint8_t *attr = resp + 20;
    attr[0] = 0x00; attr[1] = 0x20; attr[2] = 0; attr[3] = 8;
    attr[4] = 0; attr[5] = 0x01;
    uint16_t xport = (uint16_t)(54321 ^ 0x2112);
    attr[6] = (uint8_t)(xport >> 8); attr[7] = (uint8_t)xport;
    uint32_t xip = 0xCB007105u ^ 0x2112A442u;
    attr[8] = (uint8_t)(xip >> 24); attr[9] = (uint8_t)(xip >> 16); attr[10] = (uint8_t)(xip >> 8); attr[11] = (uint8_t)xip;
    NetAddr mapped = {0, 0};
    CHECK(Stun_ParseResponse(resp, 32, txid, &mapped) && mapped.ip == 0xCB007105u && mapped.port == 54321, "net: what the STUN server saw");
    uint8_t wrongId[STUN_TXID] = {9};
    CHECK(!Stun_ParseResponse(resp, 32, wrongId, &mapped), "net: an answer to someone else's question is ignored");
    CHECK(!Stun_ParseResponse(resp, 31, txid, &mapped) && !Stun_ParseResponse(resp, 20, txid, &mapped), "net: a cut-short answer is ignored");
    resp[3] = 200;
    CHECK(!Stun_ParseResponse(resp, 32, txid, &mapped), "net: an answer claiming more than it has is ignored");
    uint8_t game[8] = {'P', 'K', 1, 1, 0, 0, 0, 0};
    CHECK(!Stun_IsMessage(game, 8), "net: a game packet is never taken for STUN");

    /* a real datagram over loopback */
    int s1 = Net_Open(), s2 = Net_Open();
    CHECK(s1 >= 0 && s2 >= 0 && Net_LocalPort(s2) != 0, "net: two sockets");
    if (s1 >= 0 && s2 >= 0) {
        NetAddr to = {0x7F000001u, Net_LocalPort(s2)};
        CHECK(Net_Send(s1, to, "hello", 5), "net: a datagram goes out");
        char buf[16];
        NetAddr from = {0, 0};
        int got = 0;
        for (int k = 0; k < 200 && got == 0; k++) {
            got = Net_Recv(s2, &from, buf, sizeof(buf));
            if (!got) SleepMs(1);
        }
        CHECK(got == 5 && !memcmp(buf, "hello", 5) && from.port == Net_LocalPort(s1), "net: and arrives, from the right port");
        CHECK(Net_Recv(s2, &from, buf, sizeof(buf)) == 0, "net: and nothing else is waiting");
    }
    Net_Close(s1);
    Net_Close(s2);
    char hex[80];
    CHECK(Net_RandomHex(hex, sizeof(hex), 16) && strlen(hex) == 32, "net: random keys");
    uint32_t t0 = Net_Millis();
    SleepMs(3);
    CHECK(Net_Millis() - t0 >= 2, "net: a clock that moves");
}

#include "../src/netmsg.h"

static void test_netmsg(void) {
    char q[64];
    CHECK(Json_Quote("a\"b\\c\n", q, sizeof(q)) && !strcmp(q, "\"a\\\"b\\\\c\\n\""), "json: quotes and backslashes are escaped");
    CHECK(!Json_Quote("far too long for the space given", q, 10), "json: and it says when it doesn't fit");

    char v[64];
    bool b = false;
    CHECK(Json_GetString("{\"code\": \"ABCDEF\"}", "code", v, sizeof(v)) && !strcmp(v, "ABCDEF"), "json: the server's room code");
    CHECK(Json_GetString("{\"name\":\"x\\\",\\\"code\\\":\\\"EVIL\",\"code\":\"GOOD22\"}", "code", v, sizeof(v)) && !strcmp(v, "GOOD22"),
          "json: a key hidden inside another value is not found");
    CHECK(Json_GetString("{\"name\":\"x\\\",\\\"code\\\":\\\"EVIL\"}", "name", v, sizeof(v)) && !strcmp(v, "x\",\"code\":\"EVIL"),
          "json: escaped quotes come back as quotes");
    CHECK(Json_GetBool("{\"waiting\": true}", "waiting", &b) && b, "json: true");
    CHECK(Json_GetBool("{\"a\":{\"waiting\":true},\"waiting\":false}", "waiting", &b) && !b, "json: a nested key is not the outer one");
    CHECK(!Json_GetString("{\"code\": 5}", "code", v, sizeof(v)), "json: a number is not a string");
    CHECK(!Json_GetString("{\"code\": \"ABC", "code", v, sizeof(v)) && !Json_GetString("not json", "code", v, sizeof(v)) &&
          !Json_GetString("{\"code\" \"x\"}", "code", v, sizeof(v)), "json: broken answers give nothing");
    CHECK(Json_GetString("{\"u\":\"\\u0041\\u00e9\"}", "u", v, sizeof(v)) && !strcmp(v, "A?"), "json: \\u escapes, ASCII only");
    CHECK(!Json_GetString("{\"code\":\"0123456789\"}", "code", v, 5), "json: a value too long for the space is refused");

    const char *topic = "realtime:pk-0123456789abcdef0123456789abcdef";
    char msg[2048];
    CHECK(Rt_Join(msg, sizeof(msg), topic, 1) > 0 && Json_Find(msg, "payload") && strstr(msg, "\"self\":false"), "relay: a join message");
    uint8_t pkt[5] = {'P', 'K', 1, 0xAB, 0x00};
    int n = Rt_Broadcast(msg, sizeof(msg), topic, pkt, 5, 7);
    CHECK(n > 0 && strstr(msg, "\"d\":\"504b01ab00\""), "relay: a packet as hex");
    char seen[2048];
    snprintf(seen, sizeof(seen), "{\"event\":\"broadcast\",\"payload\":{\"event\":\"m\",\"payload\":{\"d\":\"504b01ab00\"},\"type\":\"broadcast\"},\"ref\":null,\"topic\":\"%s\"}", topic);
    uint8_t got[16];
    int glen = 0;
    CHECK(Rt_Parse(seen, topic, 1, got, sizeof(got), &glen) == RT_PACKET && glen == 5 && !memcmp(got, pkt, 5), "relay: and back, as the other machine hears it");
    CHECK(Rt_Parse(seen, "realtime:pk-other", 1, got, sizeof(got), &glen) == RT_OTHER, "relay: another room's message is ignored");
    snprintf(seen, sizeof(seen), "{\"event\":\"broadcast\",\"payload\":{\"event\":\"m\",\"payload\":{\"d\":\"50zz\"},\"type\":\"broadcast\"},\"ref\":null,\"topic\":\"%s\"}", topic);
    CHECK(Rt_Parse(seen, topic, 1, got, sizeof(got), &glen) == RT_OTHER, "relay: a message that isn't hex is ignored");
    snprintf(seen, sizeof(seen), "{\"ref\":\"1\",\"event\":\"phx_reply\",\"payload\":{\"status\":\"ok\",\"response\":{\"postgres_changes\":[]}},\"topic\":\"%s\"}", topic);
    CHECK(Rt_Parse(seen, topic, 1, got, sizeof(got), &glen) == RT_JOINED, "relay: the server lets us into the channel");
    CHECK(Rt_Parse(seen, topic, 2, got, sizeof(got), &glen) == RT_OTHER, "relay: a reply to something else isn't the join's");
    snprintf(seen, sizeof(seen), "{\"ref\":\"1\",\"event\":\"phx_reply\",\"payload\":{\"status\":\"error\",\"response\":{\"reason\":\"nope\"}},\"topic\":\"%s\"}", topic);
    CHECK(Rt_Parse(seen, topic, 1, got, sizeof(got), &glen) == RT_JOIN_FAILED, "relay: or doesn't");
    snprintf(seen, sizeof(seen), "{\"event\":\"phx_close\",\"payload\":{},\"ref\":\"1\",\"topic\":\"%s\"}", topic);
    CHECK(Rt_Parse(seen, topic, 1, got, sizeof(got), &glen) == RT_CLOSED, "relay: a closed channel is noticed");
    CHECK(Rt_Heartbeat(msg, sizeof(msg), 9) > 0 && strstr(msg, "\"topic\":\"phoenix\""), "relay: the keep-alive");

    char url[200] = "default", key[200] = "default";
    NetMsg_ConfigLine("url = https://example.supabase.co/ ", url, sizeof(url), key, sizeof(key));
    NetMsg_ConfigLine("key=sb_publishable_abc", url, sizeof(url), key, sizeof(key));
    CHECK(!strcmp(url, "https://example.supabase.co") && !strcmp(key, "sb_publishable_abc"), "server: online.conf can point elsewhere");
    NetMsg_ConfigLine("url=http://plain.example.com", url, sizeof(url), key, sizeof(key));
    NetMsg_ConfigLine("# url=https://commented.example.com", url, sizeof(url), key, sizeof(key));
    NetMsg_ConfigLine("enabled=0", url, sizeof(url), key, sizeof(key));
    CHECK(!strcmp(url, "https://example.supabase.co"), "server: but never to a plain http address, and comments are comments");
}
