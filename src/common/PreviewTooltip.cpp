#include "PreviewTooltip.h"

#include "common/Configuration.h"
#include "core/Iaito.h"

#include <QFont>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QWidget>

namespace {

static const int kMaxTooltipDisasmPreviewLines = 10;
static const int kMaxTooltipHexdumpBytes = 64;

struct RegisterPreview
{
    QString name;
    QString value;
    RVA address = RVA_INVALID;
};

QString normalizedRegisterToken(const QString &token)
{
    static const QRegularExpression trimRegExp(QStringLiteral(R"(^[^\w]+|[^\w]+$)"));
    return QString(token).remove(trimRegExp).toLower();
}

QString registerValueString(const QJsonValue &value, RVA *address)
{
    QString valueString;
    bool ok = false;
    if (value.isString()) {
        valueString = value.toString().trimmed();
        if (address) {
            *address = Core()->math(valueString);
        }
    } else {
        const RVA valueAddress = value.toVariant().toULongLong(&ok);
        if (ok) {
            valueString = RAddressString(valueAddress);
            if (address) {
                *address = valueAddress;
            }
        }
    }
    return valueString;
}

bool registerPreviewForToken(const QString &token, RegisterPreview *preview)
{
    const QString needle = normalizedRegisterToken(token);
    if (needle.isEmpty()) {
        return false;
    }

    const QVector<RegisterRefValueDescription> refs = Core()->getRegisterRefValues();
    for (const RegisterRefValueDescription &reg : refs) {
        if (reg.name.toLower() != needle || reg.value.trimmed().isEmpty()) {
            continue;
        }

        if (preview) {
            preview->name = reg.name;
            preview->value = reg.value.trimmed();
            preview->address = Core()->math(preview->value);
        }
        return true;
    }

    const QJsonObject registers = Core()->getRegisterValues().object();
    for (const QString &name : registers.keys()) {
        if (name.toLower() != needle) {
            continue;
        }

        RVA address = RVA_INVALID;
        const QString value = registerValueString(registers.value(name), &address);
        if (value.isEmpty()) {
            return false;
        }
        if (preview) {
            preview->name = name;
            preview->value = value;
            preview->address = address;
        }
        return true;
    }

    return false;
}

QString previewContainerStart()
{
    const QFont &fnt = Config()->getFont();
    return QStringLiteral(
               "<html><div style=\"font-family: %1; font-size: %2pt; white-space: nowrap;\">")
        .arg(fnt.family().toHtmlEscaped())
        .arg(qMax(6, fnt.pointSize() - 1));
}

} // namespace

QString PreviewTooltip::buildAddressPreview(RVA address)
{
    if (address == RVA_INVALID) {
        return QString();
    }

    QString mapName = Core()->cmd(QStringLiteral("dm.@ %1").arg(address)).trimmed();
    QStringList disasmPreview
        = Core()->getDisassemblyPreview(address, kMaxTooltipDisasmPreviewLines);
    QString hexPreview = Core()->getHexdumpPreview(address, kMaxTooltipHexdumpBytes);

    if (mapName.isEmpty() && disasmPreview.isEmpty() && hexPreview.isEmpty()) {
        return QString();
    }

    QString tip = previewContainerStart();
    if (!mapName.isEmpty()) {
        tip += QStringLiteral("<div style=\"margin-bottom: 6px;\"><strong>Map</strong>: %1</div>")
                   .arg(mapName.toHtmlEscaped());
    }

    if (!disasmPreview.isEmpty()) {
        tip += QStringLiteral(
                   "<div style=\"margin-bottom: 6px;\"><strong>Disassembly</strong>:<br>%1</div>")
                   .arg(disasmPreview.join("<br>"));
    }

    if (!hexPreview.isEmpty()) {
        tip += QStringLiteral("<div><strong>Hexdump</strong>:<br>%1</div>").arg(hexPreview);
    }

    tip += QStringLiteral("</div></html>");
    return tip;
}

QString PreviewTooltip::buildRegisterPreview(const QString &token)
{
    RegisterPreview preview;
    if (!registerPreviewForToken(token, &preview)) {
        return QString();
    }

    QString tip = previewContainerStart();
    tip += QStringLiteral(
               "<div style=\"margin-bottom: 6px;\"><strong>Register</strong>: %1 = %2</div>")
               .arg(preview.name.toHtmlEscaped())
               .arg(preview.value.toHtmlEscaped());

    QString addressPreview = buildAddressPreview(preview.address);
    if (!addressPreview.isEmpty()) {
        addressPreview.remove(QRegularExpression(QStringLiteral(R"(^<html><div[^>]*>)")));
        addressPreview.chop(QStringLiteral("</div></html>").size());
        if (!addressPreview.isEmpty()) {
            tip += addressPreview;
        }
    }

    tip += QStringLiteral("</div></html>");
    return tip;
}

void PreviewTooltip::applyStyleSheet(QWidget *widget, int maxWidth)
{
    if (!widget) {
        return;
    }

    widget->setStyleSheet(
        QStringLiteral(
            "QToolTip { border: 1px solid %3; border-radius: 6px; max-width: %1px;"
            " padding: 6px; opacity: 255; background-color: %2; color: %3; }")
            .arg(maxWidth)
            .arg(Config()->getColor("gui.tooltip.background").name())
            .arg(Config()->getColor("gui.tooltip.foreground").name()));
}
