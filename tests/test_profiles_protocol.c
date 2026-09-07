#define main app_id_main
#include "test_app_id.c"
#undef main
static struct reqresp exchange(struct client *c, int peer, struct reqresp req)
{
    struct reqresp response;
    assert(handle_request(c, &req) == 0);
    assert(read(peer, &response, sizeof response) == sizeof response);
    return response;
}
int main(void)
{
    struct client c = {0}, other = {0};
    struct reqresp q = {0}, a;
    struct spnav_profile_set *s = calloc(1, sizeof *s);
    int fd[2], off, n;
    assert(s && socketpair(AF_UNIX, SOCK_STREAM, 0, fd) == 0);
    c.sock = other.sock = fd[0];
    default_cfg(&cfg);
    profile_on_cfg_reload(&cfg);
    q.type = REQ_PROFILE_BEGIN;
    a = exchange(&c, fd[1], q);
    assert(a.data[0] == sizeof *s);
    for (off = 0; off < (int)sizeof *s; off += n) {
        q.type = REQ_PROFILE_READ;
        q.data[0] = off;
        a = exchange(&c, fd[1], q);
        assert(!a.data[6]);
        n = sizeof *s - off;
        if (n > 24)
            n = 24;
        memcpy((char *)s + off, a.data, n);
    }
    assert(s->version == 1 && s->count == 1);
    q.type = REQ_PROFILE_READ;
    q.data[0] = -1;
    assert(exchange(&c, fd[1], q).data[6] == -1);
    q.type = REQ_PROFILE_BEGIN;
    q.data[0] = 1;
    assert(!exchange(&c, fd[1], q).data[6]);
    q.type = REQ_PROFILE_APPLY;
    assert(exchange(&c, fd[1], q).data[6] == -1);
    q.type = REQ_PROFILE_WRITE;
    q.data[6] = 24;
    assert(exchange(&c, fd[1], q).data[6] == -1);
    for (off = 0; off < (int)sizeof *s; off += n) {
        memset(&q, 0, sizeof q);
        q.type = REQ_PROFILE_WRITE;
        q.data[6] = off;
        n = sizeof *s - off;
        if (n > 24)
            n = 24;
        memcpy(q.data, (char *)s + off, n);
        assert(!exchange(&c, fd[1], q).data[6]);
    }
    q.type = REQ_PROFILE_APPLY;
    assert(exchange(&other, fd[1], q).data[6] == -1);
    assert(!exchange(&c, fd[1], q).data[6]);
    assert(!c.profile_transfer);
    free(s);
    close(fd[0]);
    close(fd[1]);
    puts("Profile transfer bounds, isolation and complete-write checks passed");
}
