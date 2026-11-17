#include <QCoreApplication>
#include <QSslSocket>
#include <QTextStream>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTextStream(stdout) << "TLS available: " << QSslSocket::supportsSsl()
                        << "; runtime: " << QSslSocket::sslLibraryVersionString() << Qt::endl;
    return QSslSocket::supportsSsl() && !QSslSocket::sslLibraryVersionString().isEmpty() ? 0 : 1;
}
