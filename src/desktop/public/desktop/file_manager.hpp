#pragma once

#include <string_view>

namespace Klip::Desktop
{
// Opens the file's folder with the file selected; false where the desktop has no org.freedesktop.FileManager1.
bool ShowInFileManager(std::string_view fileUri);
} // namespace Klip::Desktop
