#pragma once

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QString>
#ifdef Q_OS_UNIX
#include <sys/stat.h>
#endif
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace decolog::core::fileidentity {
inline QByteArray read(const QString& path)
{
#ifdef Q_OS_UNIX
    struct stat info {};
    if (::stat(QFile::encodeName(path).constData(),&info)!=0 || !S_ISREG(info.st_mode)) return {};
    return QByteArray::number(qulonglong(info.st_dev))+':'+QByteArray::number(qulonglong(info.st_ino));
#elif defined(Q_OS_WIN)
    const HANDLE handle=CreateFileW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(path).utf16()),FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if (handle==INVALID_HANDLE_VALUE) return {};
    BY_HANDLE_FILE_INFORMATION info {}; const bool ok=GetFileInformationByHandle(handle,&info); CloseHandle(handle);
    if (!ok || (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) return {};
    return QByteArray::number(qulonglong(info.dwVolumeSerialNumber))+':'+QByteArray::number(qulonglong(info.nFileIndexHigh))+':'+QByteArray::number(qulonglong(info.nFileIndexLow));
#else
    Q_UNUSED(path); return {};
#endif
}
}
