#ifndef PAYMENTREQUESTPLUS_H
#define PAYMENTREQUESTPLUS_H

#include <QByteArray>
#include <QList>
#include <QString>

#include "base58.h"
#undef loop
#include "paymentrequest.pb.h"

//
// Wraps dumb protocol buffer paymentRequest
// with extra methods
//

class PaymentRequestPlus
{
public:
    PaymentRequestPlus() { }
    PaymentRequestPlus(const QByteArray& data);

    bool IsInitialized() const;
    QString getPKIType() const;
    // Returns true if merchant's identity is authenticated
    bool getMerchant(X509_STORE* certStore, QString& merchant) const;

    qint64 getAmountRequested() const;
    QList<CBitcoinAddress> getAddresses() const;

    const payments::PaymentDetails& getDetails() const { return details; }

private:
    payments::PaymentRequest paymentRequest;
    payments::PaymentDetails details;
};

#endif // PAYMENTREQUESTPLUS_H
