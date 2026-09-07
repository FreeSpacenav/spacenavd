/* Test the socket request handler without starting a daemon or touching hardware. */
#include <assert.h>
#include <stdio.h>
#include "../src/proto_unix.c"
struct cfg cfg, prev_cfg;
struct profile profiles[MAX_PROFILES];
int num_profiles;
char *cfgfile;
const char *(*kbemu_keyname)(unsigned int);
static int updates;
void lcd_idle_reset(void) {}
void led_idle_reset(void) {}
int lcd_supported(void) { return 1; }
int lcd_refresh(void) { updates++; return 0; }
void logmsg(int priority, const char *fmt, ...) { (void)priority; (void)fmt; }
void lcd_update_mappings(void) { updates++; }
void cfg_changed(void) {}
int read_cfg(const char *path, struct cfg *c) { (void)path; (void)c; return -1; }
int write_cfg(const char *path, struct cfg *c) { (void)path; (void)c; return -1; }
void default_cfg(struct cfg *c) { memset(c, 0, sizeof *c); num_profiles = 0; }
int get_client_socket(struct client *c) { return c->sock; }
void set_client_sensitivity(struct client *c, float s) { c->sens = s; }
float get_client_sensitivity(struct client *c) { return c->sens; }
struct device *get_client_device(struct client *c) { return c->dev; }
static void identify(struct client *c, const char *id)
{
	struct reqresp req;
	memset(&req, 0, sizeof req);
	req.type = REQ_SET_APP_ID;
	assert(strlen(id) < 24);
	memcpy(req.data, id, strlen(id)); req.data[6] = strlen(id);
	assert(handle_request(c, &req) == 0);
	assert(c->app_id && !strcmp(c->app_id, id));
}
int main(void)
{
	struct client a = {0}, b = {0};
	struct reqresp req, reply;
	int sockets[2];
	cfg.sensitivity = 1;
	profiles[0].name = "Blender"; profiles[0].match_class = "blender";
	profiles[0].pcfg.sensitivity = 2; num_profiles = 1;
	profile_on_cfg_reload(&cfg);
	identify(&a, "blender");
	assert(profile_active_index() == -1 && cfg.sensitivity == 1 && updates == 0);
	identify(&b, "another-app");
	assert(profile_active_index() == -1 && updates == 0);
	profile_set_manual(0);
	identify(&b, "background-app");
	assert(profile_active_index() == 0 && updates == 0);
	assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
	a.sock = sockets[0];
	memset(&req, 0, sizeof req); req.type = REQ_CFG_RESET;
	assert(handle_request(&a, &req) == 0);
	assert(read(sockets[1], &reply, sizeof reply) == sizeof reply);
	assert(profile_active_index() == -1 && updates == 1);
	/* A failed restore falls back to defaults and must also reset profiles. */
	num_profiles = 1; profile_set_manual(0);
	req.type = REQ_CFG_RESTORE;
	assert(handle_request(&a, &req) == 0);
	assert(read(sockets[1], &reply, sizeof reply) == sizeof reply);
	assert(profile_active_index() == -1 && updates == 2);
	req.type = REQ_SCFG_LCD; req.data[0] = 2;
	assert(handle_request(&a, &req) == 0);
	assert(read(sockets[1], &reply, sizeof reply) == sizeof reply);
	assert(reply.data[6] == 0 && cfg.lcd_flags == 2);
	req.type = REQ_SCFG_LCD; req.data[0] = 4;
	assert(handle_request(&a, &req) == 0);
	assert(read(sockets[1], &reply, sizeof reply) == sizeof reply);
	assert(reply.data[6] == -1 && cfg.lcd_flags == 2);
	req.type = REQ_GCFG_LCD;
	assert(handle_request(&a, &req) == 0);
	assert(read(sockets[1], &reply, sizeof reply) == sizeof reply);
	assert(reply.data[6] == 0 && reply.data[0] == 2);
	req.type = REQ_SCFG_LCD_IDLE; req.data[0] = 120;
	assert(handle_request(&a, &req) == 0);
	assert(read(sockets[1], &reply, sizeof reply) == sizeof reply);
	assert(reply.data[6] == 0 && cfg.lcd_idle_seconds == 120);
	req.data[0] = 86401;
	assert(handle_request(&a, &req) == 0);
	assert(read(sockets[1], &reply, sizeof reply) == sizeof reply);
	assert(reply.data[6] == -1 && cfg.lcd_idle_seconds == 120);
	req.type = REQ_GCFG_LCD_IDLE;
	assert(handle_request(&a, &req) == 0);
	assert(read(sockets[1], &reply, sizeof reply) == sizeof reply);
	assert(reply.data[6] == 0 && reply.data[0] == 120);
	req.type = REQ_SCFG_LCD_BRIGHTNESS; req.data[0] = 40;
	assert(handle_request(&a, &req) == 0);
	assert(read(sockets[1], &reply, sizeof reply) == sizeof reply);
	assert(reply.data[6] == 0 && cfg.lcd_brightness == 40);
	req.data[0] = 101;
	assert(handle_request(&a, &req) == 0);
	assert(read(sockets[1], &reply, sizeof reply) == sizeof reply);
	assert(reply.data[6] == -1 && cfg.lcd_brightness == 40);
	memset(&req, 0, sizeof req); req.type = REQ_SET_FOCUS;
	memcpy(req.data, "blender", 7); req.data[6] = 7;
	assert(handle_request(&a, &req) == 0);
	assert(read(sockets[1], &reply, sizeof reply) == sizeof reply && reply.data[6] == 0);
	req.data[6] = 256;
	assert(handle_request(&a, &req) == 0);
	assert(read(sockets[1], &reply, sizeof reply) == sizeof reply && reply.data[6] == -1);
	profile_clear_focus(&a);
	req.type = REQ_SCFG_LED_IDLE; req.data[0] = 15;
	assert(handle_request(&a, &req) == 0);
	assert(read(sockets[1], &reply, sizeof reply) == sizeof reply && reply.data[6] == 0);
	assert(cfg.led_idle_seconds == 15);
	req.data[0] = -1;
	assert(handle_request(&a, &req) == 0);
	assert(read(sockets[1], &reply, sizeof reply) == sizeof reply && reply.data[6] == -1);
	close(sockets[0]); close(sockets[1]);
	free(a.app_id); free(b.app_id);
	puts("Application ID tests passed");
	return 0;
}
