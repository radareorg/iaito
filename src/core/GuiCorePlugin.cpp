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

static bool uiaitoParseRangeArgument(const char *input, RVA *start, RVA *end, QString *error)
{
    const QString args = QString::fromUtf8(input).simplified();
    const QStringList parts = args.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (parts.isEmpty() || parts.size() > 2) {
        *error = QStringLiteral("Usage: uis [address] ([length])");
        return false;
    }

    const RVA address = Core()->math(parts.at(0));
    if (address == RVA_INVALID) {
        *error = QStringLiteral("Invalid address");
        return false;
    }

    const RVA length = parts.size() == 2 ? Core()->math(parts.at(1)) : 1;
    if (length == 0 || length == RVA_INVALID) {
        *error = QStringLiteral("Invalid length");
        return false;
    }
    if (length - 1 > RVA_MAX - address) {
        *error = QStringLiteral("Selection range overflows");
        return false;
    }

    *start = address;
    *end = address + length - 1;
    return true;
}

static void uiaitoPrintUsage(RCore *core)
{
    static const char *usage = "Usage: ui[..] [..args] - uiaito interactions\n"
                               "| ui [message]       - show popup dialog with given message\n"
                               "| uid ([path])       - select directory and print it\n"
                               "| uif ([path])       - select file and print it\n"
                               "| uig [offset]       - goto offset in UI\n"
                               "| uip ([name])       - list panels or focus panel by name\n"
                               "| uir                - refresh UI contents\n"
                               "| uis [addr] ([len]) - select byte range in UI\n";

    r_cons_printf(core->cons, "%s", usage);
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
        case 's': {
            MainWindow *mainWindow = uiaitoMainWindow();
            if (!mainWindow) {
                r_core_return_code(core, 1);
                r_cons_printf(core->cons, "No iaito main window available\n");
                break;
            }

            RVA start = RVA_INVALID;
            RVA end = RVA_INVALID;
            QString error;
            if (!uiaitoParseRangeArgument(input + 3, &start, &end, &error)) {
                r_core_return_code(core, 1);
                r_cons_printf(core->cons, "%s\n", error.toUtf8().constData());
                break;
            }

            mainWindow->gotoOffset(RAddressString(start));
            Core()->setAddressRangeSelection(start, end);
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
            uiaitoPrintUsage(core);
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
