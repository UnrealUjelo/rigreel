// RE Engine .mdf2 (material definition) reader -> neutral materials with normalized texture slots.
#pragma once

#include <director/Assets.h>

namespace re {

QVector<dir::MaterialAsset> parseMdf(const QByteArray &data, int fileVersion, QString *error);

} // namespace re
