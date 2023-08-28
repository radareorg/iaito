#include <r_core.h>

extern int main_iaito();
static int r_cmd_anal_call(void *user, const char *input) {
	RCore *core = (RCore*)user;
	if (r_str_startswith (input, "iaito")) {
		char *n = r_str_newf ("%p", user);
		r_sys_setenv ("R2COREPTR", n);
		free (n);
		main_iaito ();
		return true;
	}
	return false;
}
extern "C" {
// PLUGIN Definition Info
RCorePlugin r_core_plugin_a2f = {
	.meta = {
		.name = "a2f",
		.desc = "The reworked analysis from scratch thing",
		.license = "LGPL3",
	},
	.call = r_cmd_anal_call,
};

#ifndef R2_PLUGIN_INCORE
R_API RLibStruct radare_plugin = {
	.type = R_LIB_TYPE_CORE,
	.data = &r_core_plugin_a2f,
	.version = R2_VERSION
};
#endif

}
