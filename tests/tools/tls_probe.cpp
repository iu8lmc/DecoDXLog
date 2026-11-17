#include <QCoreApplication>
#include <QSslSocket>
#include <QTextStream>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const bool openssl = QSslSocket::setActiveBackend(QStringLiteral("openssl"));
    QTextStream(stdout) << "OpenSSL backend: " << openssl << "; TLS available: " << QSslSocket::supportsSsl()
                        << "; runtime: " << QSslSocket::sslLibraryVersionString() << Qt::endl;
    return openssl && QSslSocket::supportsSsl() && !QSslSocket::sslLibraryVersionString().isEmpty() ? 0 : 1;
}
