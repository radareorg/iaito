#ifndef PREVIEWTOOLTIP_H
#define PREVIEWTOOLTIP_H

#include "core/IaitoCommon.h"

#include <QString>

class QWidget;

namespace PreviewTooltip {

QString buildAddressPreview(RVA address);
QString buildRegisterPreview(const QString &token);
void applyStyleSheet(QWidget *widget, int maxWidth = 420);

} // namespace PreviewTooltip

#endif // PREVIEWTOOLTIP_H
