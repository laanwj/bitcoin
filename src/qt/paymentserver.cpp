// Copyright (c) 2009-2012 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QByteArray>
#include <QDataStream>
#include <QDebug>
#include <QFile>
#include <QFileOpenEvent>
#include <QHash>
#include <QList>
#include <QLocalServer>
#include <QLocalSocket>
#include <QStringList>
#if QT_VERSION < 0x050000
#include <QUrl>
#else
#include <QUrlQuery>
#endif
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslCertificate>
#include <QSslError>
#include <QSslSocket>

#include <cstdlib>

#include <openssl/x509.h>
#include <openssl/x509_vfy.h>

#include "base58.h"
#include "guiconstants.h"
#include "guiutil.h"
#include "paymentserver.h"
#include "ui_interface.h"
#include "util.h"
#include "wallet.h"
#include "walletmodel.h"

using namespace boost;

const int BITCOIN_IPC_CONNECT_TIMEOUT = 1000; // milliseconds
const QString BITCOIN_IPC_PREFIX("bitcoin:");

X509_STORE* PaymentServer::certStore = NULL;
void PaymentServer::freeCertStore()
{
    if (PaymentServer::certStore != NULL)
    {
        X509_STORE_free(PaymentServer::certStore);
        PaymentServer::certStore = NULL;
    }
}

//
// Create a name that is unique for:
//  testnet / non-testnet
//  data directory
//
static QString ipcServerName()
{
    QString name("BitcoinQt");

    // Append a simple hash of the datadir
    // Note that GetDataDir(true) returns a different path
    // for -testnet versus main net
    QString ddir(GetDataDir(true).string().c_str());
    name.append(QString::number(qHash(ddir)));

    return name;
}

//
// We store payment URLs and requests received before
// the main GUI window is up and ready to ask the user
// to send payment.

static QList<QString> savedPaymentRequests;

//
// Load openSSL's list of root certificate authorities
//
void PaymentServer::LoadRootCAs(X509_STORE* _store)
{
    if (PaymentServer::certStore == NULL)
        atexit(PaymentServer::freeCertStore);
    else
        freeCertStore();

    // Unit tests mostly use this, to pass in fake root CAs:
    if (_store)
    {
        PaymentServer::certStore = _store;
        return;
    }

    // Normal execution, use either -rootcertificates or system certs:
    PaymentServer::certStore = X509_STORE_new();

    // Note: use "-system-" default here so that users can pass -rootcertificates=""
    // and get 'I don't like X.509 certificates, don't trust anybody' behavior:
    QString certFile = QString::fromStdString(GetArg("-rootcertificates", "-system-"));

    if (certFile.isEmpty())
        return; // Empty store

    QList<QSslCertificate> certList;

    if (certFile != "-system-")
    {
        certList = QSslCertificate::fromPath(certFile);
        // Use those certificates when fetching payment requests, too:
        QSslSocket::setDefaultCaCertificates(certList);
    }
    else
        certList = QSslSocket::systemCaCertificates ();

    // It'd be nifty if QtNetwork exposed code to do the low-level
    // certificate validation / signature checking we need,
    // but that is all buried inside the QSslSocket class.
    // So:
    const QDateTime currentTime = QDateTime::currentDateTime();
    foreach (const QSslCertificate& cert, certList)
    {
        if (currentTime < cert.effectiveDate() ||
            currentTime > cert.expiryDate() ||
            cert.isBlacklisted())
        {
            if (fDebug) {
#if QT_VERSION >= 0x050000
                QStringList s = cert.subjectInfo(QSslCertificate::CommonName);
                OutputDebugStringF("Invalid system certificate: %s\n", s.join("\n").toStdString().c_str());
#else
                QString s = cert.subjectInfo(QSslCertificate::CommonName);
                OutputDebugStringF("Invalid system certificate: %s\n", s.toStdString().c_str());
#endif
            }
            continue;
        }

        QByteArray certData = cert.toDer();
        const unsigned char *data = (const unsigned char *)certData.data();

        X509* x509 = d2i_X509(0, &data, certData.size());
        if (x509)
        {
            X509_STORE_add_cert( PaymentServer::certStore, x509);
            // X509_STORE_free will free the X509* objects when
            // the PaymentServer is destroyed
        }
    }

    // Project for another day:
    // Fetch certificate revocation lists, and add them to certStore.
    // Issues to consider:
    //   performance (start a thread to fetch in background?)
    //   privacy (fetch through tor/proxy so IP address isn't revealed)
    //   would it be easier to just use a compiled-in blacklist?
    //    or use Qt's blacklist?
}

//
// Sending to the server is done synchronously, at startup.
// If the server isn't already running, startup continues,
// and the items in savedPaymentRequest will be handled
// when uiReady() is called.
//
bool PaymentServer::ipcSendCommandLine(int argc, char* argv[])
{
    bool fResult = false;

    for (int i = 1; i < argc; i++)
    {
        QString arg(argv[i]);
        if (arg.startsWith("-"))
            continue;

        if (arg.startsWith(BITCOIN_IPC_PREFIX, Qt::CaseInsensitive)) // bitcoin:
        {
            savedPaymentRequests.append(arg);

            SendCoinsRecipient r;
            if (GUIUtil::parseBitcoinURI(arg, &r))
            {
                CBitcoinAddress address(r.address.toStdString());
                if (address.IsValid(true))
                    fTestNet = true;
            }
        }
        else if (QFile::exists(arg)) // Filename
        {
            savedPaymentRequests.append(arg);

            SendCoinsRecipient r;
            if (ReadPaymentRequest(arg, r))
                fTestNet = !(r.paymentRequest.getDetails().network() == "main");
        }
        else
        {
            OutputDebugStringF("Payment request file does not exist: %s\n", argv[i]);
            // Printing to debug.log is about the best we can do here, the
            // GUI hasn't started yet so we can't pop up a message box.
        }
    }
    if (fTestNet)
        // get a testnet URI: or payment request: need to run as -testnet
        mapArgs["-testnet"] = std::string("1");

    foreach (const QString& r, savedPaymentRequests)
    {
        QLocalSocket* socket = new QLocalSocket();
        socket->connectToServer(ipcServerName(), QIODevice::WriteOnly);
        if (!socket->waitForConnected(BITCOIN_IPC_CONNECT_TIMEOUT))
            return false;

        QByteArray block;
        QDataStream out(&block, QIODevice::WriteOnly);
        out.setVersion(QDataStream::Qt_4_0);
        out << r;
        out.device()->seek(0);
        socket->write(block);
        socket->flush();

        socket->waitForBytesWritten(BITCOIN_IPC_CONNECT_TIMEOUT);
        socket->disconnectFromServer();
        delete socket;
        fResult = true;
    }
    return fResult;
}

PaymentServer::PaymentServer(QObject* parent, bool startLocalServer) : QObject(parent), saveURIs(true)
{
    // Verify that the version of the library that we linked against is
    // compatible with the version of the headers we compiled against.
    GOOGLE_PROTOBUF_VERIFY_VERSION;

    // Install global event filter to catch QFileOpenEvents on the mac (sent when you click bitcoin: links)
    if (parent)
        parent->installEventFilter(this);

    QString name = ipcServerName();

    // Clean up old socket leftover from a crash:
    QLocalServer::removeServer(name);

    if (startLocalServer)
    {
        uriServer = new QLocalServer(this);

        if (!uriServer->listen(name))
            OutputDebugStringF("Cannot start bitcoin: click-to-pay handler");
        else
            connect(uriServer, SIGNAL(newConnection()), this, SLOT(handleURIConnection()));
    }

    // netManager is used to fetch paymentrequests given in bitcoin: URI's
    netManager = new QNetworkAccessManager(this);
    connect(netManager, SIGNAL(finished(QNetworkReply*)),
            this, SLOT(netRequestFinished(QNetworkReply*)));
    connect(netManager, SIGNAL(sslErrors(QNetworkReply*, const QList<QSslError> &)),
            this, SLOT(reportSslErrors(QNetworkReply*, const QList<QSslError> &)));
    // TODO: Connect proxy options to the netManager
    //    netManager->setProxy(...)
    // ... and need a slot so if user changes proxy netManager
    // does the right thing.
}

PaymentServer::~PaymentServer()
{
    google::protobuf::ShutdownProtobufLibrary();
}

//
// OSX-specific way of handling bitcoin uris and
// PaymentRequest mime types
//
bool PaymentServer::eventFilter(QObject *, QEvent *event)
{
    // clicking on bitcoin: URLs creates FileOpen events on the Mac:
    if (event->type() == QEvent::FileOpen)
    {
        QFileOpenEvent* fileEvent = static_cast<QFileOpenEvent*>(event);
        if (!fileEvent->file().isEmpty())
            handleURIOrFile(fileEvent->file());
        else if (!fileEvent->url().isEmpty())
            handleURIOrFile(fileEvent->url().toString());

        return true;
    }
    return false;
}

void PaymentServer::uiReady()
{
    saveURIs = false;
    foreach (const QString& s, savedPaymentRequests)
    {
        handleURIOrFile(s);
    }
    savedPaymentRequests.clear();
}

void PaymentServer::handleURIOrFile(const QString& s)
{
    if (saveURIs)
    {
        savedPaymentRequests.append(s);
        return;
    }

    if (s.startsWith(BITCOIN_IPC_PREFIX, Qt::CaseInsensitive)) // bitcoin:
    {
        QUrl url(s);
        QUrlQuery query(url);
        if (query.hasQueryItem("request"))
        {
            QByteArray temp; temp.append(query.queryItemValue("request"));
            QString decoded = QUrl::fromPercentEncoding(temp);
            QUrl fetchUrl(decoded, QUrl::StrictMode);

            printf("PaymentServer::fetchRequest %s\n", fetchUrl.toString().toStdString().c_str());
            printf(" scheme: %s host: %s path: %s\n",
                   fetchUrl.scheme().toStdString().c_str(),
                   fetchUrl.host().toStdString().c_str(),
                   fetchUrl.path().toStdString().c_str());

            if (fetchUrl.isValid())
                fetchRequest(fetchUrl);
            else
                printf("Error, invalid payment request url: %s\n", fetchUrl.toString().toStdString().c_str());
            return;
        }

        SendCoinsRecipient recipient;
        if (GUIUtil::parseBitcoinURI(s, &recipient))
            emit receivedPaymentRequest(recipient);
        return;
    }

    if (QFile::exists(s))
    {
        SendCoinsRecipient recipient;
        if (ReadPaymentRequest(s, recipient))
            emit receivedPaymentRequest(recipient);
        return;
    }
}

void PaymentServer::handleURIConnection()
{
    QLocalSocket *clientConnection = uriServer->nextPendingConnection();

    while (clientConnection->bytesAvailable() < (int)sizeof(quint32))
        clientConnection->waitForReadyRead();

    connect(clientConnection, SIGNAL(disconnected()),
            clientConnection, SLOT(deleteLater()));

    QDataStream in(clientConnection);
    in.setVersion(QDataStream::Qt_4_0);
    if (clientConnection->bytesAvailable() < (int)sizeof(quint16)) {
        return;
    }
    QString message;
    in >> message;

    handleURIOrFile(message);
}

bool PaymentServer::ReadPaymentRequest(const QString& filename, SendCoinsRecipient& recipient)
{
    QFile f(filename);
    if (!f.open(QIODevice::ReadOnly))
    {
        OutputDebugStringF("PaymentServer::ReadPaymentRequest fail to open %s\n",
                           filename.toStdString().c_str());
        return false;
    }

    if (f.size() > MAX_PAYMENT_REQUEST_SIZE)
    {
        OutputDebugStringF("PaymentServer::ReadPaymentRequest %s too large",
                           filename.toStdString().c_str());
        return false;
    }

    QByteArray data = f.readAll();

    PaymentRequestPlus request(data);

    return processPaymentRequest(request, recipient);
}

bool
PaymentServer::processPaymentRequest(PaymentRequestPlus& request, SendCoinsRecipient& recipient)
{
    const payments::PaymentDetails& details = request.getDetails();
    recipient.paymentRequest = request;

    // Expired?
    if (details.has_expires() && (int64)details.expires() < GetTime())
    {
        recipient.error = tr("Payment request expired");
        return false;
    }
    std::string network = details.network();
    if (fTestNet && network == "main") {
        recipient.error = tr("Main network payment request received: running in testnet mode");
        return false;
    }
    if (!fTestNet && network == "test") {
        recipient.error = tr("Test network payment request received: not running in testnet mode");
        return false;
    }

    recipient.amount = request.getAmountRequested();
    foreach(const CBitcoinAddress& addr, request.getAddresses())
        recipient.address.append(QString::fromStdString(addr.ToString())+",");
    if (!recipient.address.isEmpty()) recipient.address.chop(1); // remove last comma

    request.getMerchant(PaymentServer::certStore, recipient.authenticatedMerchant);

    return request.IsInitialized();
}

void
PaymentServer::fetchRequest(const QUrl& url)
{
    QNetworkRequest netRequest;
    netRequest.setAttribute(QNetworkRequest::User, "PaymentRequest");
    netRequest.setUrl(url);
    netRequest.setRawHeader("User-Agent", CLIENT_NAME.c_str());
    netManager->get(netRequest);
}

void
PaymentServer::fetchPaymentACK(CWallet* wallet, SendCoinsRecipient recipient, QByteArray transaction)
{
    if (recipient.authenticatedMerchant.isEmpty())
        return; // Only fetch PaymentACKs from authenticated merchants

    const payments::PaymentDetails& details = recipient.paymentRequest.getDetails();
    if (!details.has_payment_url())
        return;

    QNetworkRequest netRequest;
    netRequest.setAttribute(QNetworkRequest::User, "PaymentACK");
    netRequest.setUrl(QString::fromStdString(details.payment_url()));
    netRequest.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-bitcoin-payment");
    netRequest.setRawHeader("User-Agent", CLIENT_NAME.c_str());

    payments::Payment payment;
    payment.set_merchant_data(details.merchant_data());
    payment.add_transactions(transaction.data(), transaction.size());

    // Get a new key to use for refunds:
    CPubKey newKey;
    if (wallet->GetKeyFromPool(newKey, false)) {
        CKeyID keyID = newKey.GetID();
        QString account = tr("Refund from") + QString(" ") + recipient.authenticatedMerchant;
        wallet->SetAddressBookName(keyID, account.toStdString());

        CScript s; s.SetDestination(keyID);
        payments::Output* refund_to = payment.add_refund_to();
        refund_to->set_script(&s[0], s.size());
    }
    else {
        // This should never happen, because sending coins should have just unlocked the wallet
        // and refilled the keypool
        OutputDebugStringF("Error getting refund key, refund_to not set");
    }

    int length = payment.ByteSize();
    netRequest.setHeader(QNetworkRequest::ContentLengthHeader, length);
    QByteArray serData(length, '\0');
    if (payment.SerializeToArray(serData.data(), length)) {
        netManager->post(netRequest, serData);
    }
    else {
        // This should never happen, either:
        OutputDebugStringF("Error serializing payment message");
    }
}

void
PaymentServer::netRequestFinished(QNetworkReply* reply)
{
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError)
    {
        QString message = QObject::tr("Error communicating with %1: %2")
            .arg(reply->request().url().toString())
            .arg(reply->errorString());
        OutputDebugStringF("%s\n", message.toStdString().c_str());
        emit reportError(message, tr("Network request error"), CClientUIInterface::MODAL);
        return;
    }

    QByteArray data = reply->readAll();

    QString requestType = reply->request().attribute(QNetworkRequest::User).toString();
    if (requestType == "PaymentRequest")
    {
        PaymentRequestPlus request(data);
        SendCoinsRecipient recipient;
        processPaymentRequest(request, recipient);
        emit receivedPaymentRequest(recipient);
        return;
    }
    else if (requestType == "PaymentACK")
    {
        payments::PaymentACK paymentACK;
        if (!paymentACK.ParseFromArray(data.data(), data.size()))
        {
            QString message = QObject::tr("Bad response from server %1")
                .arg(reply->request().url().toString());
            OutputDebugStringF("%s\n", message.toStdString().c_str());
            emit reportError(message, tr("Network request error"), CClientUIInterface::MODAL);
        }
        else {
            emit receivedPaymentACK(QString::fromStdString(paymentACK.memo()));
        }
    }
}

void
PaymentServer::reportSslErrors(QNetworkReply* reply, const QList<QSslError> &errs)
{
    QString errString;
    foreach (const QSslError& err, errs) {
        errString += err.errorString() + "\n";
    }
    OutputDebugStringF("%s", errString.toStdString().c_str());
    emit reportError(errString, tr("Network request error"), CClientUIInterface::MODAL);
}
