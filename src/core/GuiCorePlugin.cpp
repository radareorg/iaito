#include "Iaito.h"
#include "IaitoApplication.h"
#include "common/Radare2Compat.h"
#include <QFileDialog>
#include <QMessageBox>

static MainWindow *uiaitoMainWindow()
{
    auto *app = qobject_cast<IaitoApplication *>(qApp);
    return app ? app->getMainWindow() : nullptr;
}

static QString uiaitoStringArgument(const char *input)
{
    QString arg = QString::fromUtf8(input).trimmed();
    if (arg.size() >= 2) {
        const QChar first = arg.at(0);
        const QChar last = arg.at(arg.size() - 1);
        if ((first == QLatin1Char('"') && last == QLatin1Char('"'))
            || (first == QLatin1Char('\'') && last == QLatin1Char('\''))) {
            arg = arg.mid(1, arg.size() - 2).trimmed();
        }
    }
    return arg;
}

static bool r2plugin_ui_call(RCorePluginSession *cps, const char *input)
{
    RCore *core = cps->core;
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
                r_cons_printf(core->cons, "%s\n", filename.toUtf8().constData());
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
                r_cons_printf(core->cons, "%s\n", filename.toUtf8().constData());
            }
        } break;
        case 'g': {
            MainWindow *mainWindow = uiaitoMainWindow();
            if (!mainWindow) {
                r_core_return_code(core, 1);
                r_cons_printf(core->cons, "No iaito main window available\n");
                break;
            }

            const QString offset = uiaitoStringArgument(input + 3);
            if (offset.isEmpty()) {
                r_core_return_code(core, 1);
                r_cons_printf(core->cons, "Usage: uig [offset]\n");
                break;
            }

            mainWindow->gotoOffset(offset);
            r_core_return_code(core, 0);
        } break;
        case 'r': {
            Core()->triggerRefreshAll();
            r_core_return_code(core, 0);
        } break;
        case 'p': {
            MainWindow *mainWindow = uiaitoMainWindow();
            if (!mainWindow) {
                r_core_return_code(core, 1);
                r_cons_printf(core->cons, "No iaito main window available\n");
                break;
            }

            const QString panelName = uiaitoStringArgument(input + 3);
            if (panelName.isEmpty()) {
                const QStringList panelNames = mainWindow->getPanelNames();
                for (const QString &name : panelNames) {
                    r_cons_printf(core->cons, "%s\n", name.toUtf8().constData());
                }
                r_core_return_code(core, 0);
                break;
            }

            if (!mainWindow->focusPanelByName(panelName)) {
                r_core_return_code(core, 1);
                r_cons_printf(core->cons, "Panel not found: %s\n", panelName.toUtf8().constData());
                break;
            }

            r_core_return_code(core, 0);
        } break;
        default:
            r_cons_printf(core->cons, "Usage: ui[..] [..args] - uiaito interactions\n");
            r_cons_printf(
                core->cons,
                "| ui [message]       - show popup dialog with given "
                "message\n");
            r_cons_printf(core->cons, "| uid ([path])       - select directory and print it\n");
            r_cons_printf(core->cons, "| uif ([path])       - select file and print it\n");
            r_cons_printf(core->cons, "| uig [offset]       - goto offset in UI\n");
            r_cons_printf(
                core->cons, "| uip ([name])       - list panels or focus panel by name\n");
            r_cons_printf(core->cons, "| uir                - refresh UI contents\n");
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

static RLibStruct uiaito_radare_plugin
    = {.type = R_LIB_TYPE_CORE,
       .data = &r_core_plugin_uiaito,
       .version = R2_VERSION,
       .pkgname = "uiaito",
       .abiversion = R2_ABIVERSION};
}
