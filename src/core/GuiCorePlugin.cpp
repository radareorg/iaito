#include "Iaito.h"
#include <r_core.h>
#include <QFileDialog>
#include <QMessageBox>

#if R2_VERSION_NUMBER >= 50909
static bool r2plugin_ui_call(RCorePluginSession *cps, const char *input)
#else
static int r2plugin_ui_call(void *user, const char *input)
#endif
{
#if R2_VERSION_NUMBER >= 50909
    RCore *core = cps->core;
#else
    IaitoCore *iaito = (IaitoCore *) user;
    RCore *core = iaito->core_;
#endif
    if (r_str_startswith(input, "ui")) {
        switch (input[2]) {
        case ' ': {
            QMessageBox msgBox;
            msgBox.setText(input + 3);
            msgBox.exec();
        } break;
        case 'd': {
            QString filename = QFileDialog::getExistingDirectory(
                nullptr,
                "Select directory",
                "", // Config()->getRecentFolder(),
                QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
            if (filename.isEmpty()) {
                r_core_return_code(core, 1);
            } else {
                r_core_return_code(core, 0);
#if R2_VERSION_NUMBER >= 50909
                r_cons_printf(core->cons, "%s\n", filename.toUtf8().constData());
#else
                r_cons_printf("%s\n", filename.toUtf8().constData());
#endif
            }
        } break;
        case 'f': {
            QString filename = QFileDialog::getOpenFileName(
                nullptr,
                "Select file",
                "", // Config()->getRecentFolder(),
                "All files (*)",
                // "Header files (*.h *.hpp);;All files (*)",
                0,
                QFILEDIALOG_FLAGS);
            if (filename.isEmpty()) {
                r_core_return_code(core, 1);
            } else {
                r_core_return_code(core, 0);
#if R2_VERSION_NUMBER >= 50909
                r_cons_printf(core->cons, "%s\n", filename.toUtf8().constData());
#else
                r_cons_printf("%s\n", filename.toUtf8().constData());
#endif
            }
        } break;
        default:
#if R2_VERSION_NUMBER >= 50909
            r_cons_printf(core->cons, "Usage: ui[..] [..args] - uiaito interactions\n");
            r_cons_printf(
                core->cons,
                "| ui [message]       - show popup dialog with given "
                "message\n");
            r_cons_printf(core->cons, "| uid ([path])       - select directory and print it\n");
            r_cons_printf(core->cons, "| uif ([path])       - select file and print it\n");
#else
            r_cons_printf("Usage: ui[..] [..args] - uiaito interactions\n");
            r_cons_printf("| ui [message]       - show popup dialog with given "
                          "message\n");
            r_cons_printf("| uid ([path])       - select directory and print it\n");
            r_cons_printf("| uif ([path])       - select file and print it\n");
#endif
            break;
        }
        return true;
    }
    return false;
}

extern "C" {
// Plugin Definition Info
RCorePlugin r_core_plugin_uiaito = {
#if 0
		// when not supported by the compiler
		{
			(char *)"ui",
			(char *)"Interact with iaito UI from the r2 shell",
			(char *)"pancake",
			(char *)"0.1",
			(char *)"LGPL3",
			R_PLUGIN_STATUS_BASIC,
		},
		r_cmd_anal_call,
		NULL,
		NULL,
#else
    .meta =
        {
            .name = (char *)"ui",
            .desc = (char *)"Interact with iaito UI from the r2 shell",
            .license = (char *)"LGPL3",
        },
    .call = r2plugin_ui_call,
#endif
};

#if R2_VERSION_NUMBER >= 50909
static RLibStruct uiaito_radare_plugin
    = {.type = R_LIB_TYPE_CORE,
       .data = &r_core_plugin_uiaito,
       .version = R2_VERSION,
       .pkgname = "uiaito",
       .abiversion = R2_ABIVERSION};
#else
static RLibStruct uiaito_radare_plugin
    = {.type = R_LIB_TYPE_CORE, .data = &r_core_plugin_uiaito, .version = R2_VERSION};
#endif
}
