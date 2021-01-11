// Copyright (c) 2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <wallet/externalsigner.h>

ExternalSigner::ExternalSigner(const std::string& command, const std::string& fingerprint, bool mainnet, std::string name): m_command(command), m_fingerprint(fingerprint), m_mainnet(mainnet), m_name(name) {}

#ifdef ENABLE_EXTERNAL_SIGNER

bool ExternalSigner::Enumerate(const std::string& command, std::vector<ExternalSigner>& signers, bool mainnet, bool ignore_errors)
{
    // Call <command> enumerate
    const UniValue result = RunCommandParseJSON(command + " enumerate");
    if (!result.isArray()) {
        if (ignore_errors) return false;
        throw ExternalSignerException(strprintf("'%s' received invalid response, expected array of signers", command));
    }
    for (UniValue signer : result.getValues()) {
        // Check for error
        const UniValue& error = find_value(signer, "error");
        if (!error.isNull()) {
            if (ignore_errors) return false;
            if (!error.isStr()) {
                throw ExternalSignerException(strprintf("'%s' error", command));
            }
            throw ExternalSignerException(strprintf("'%s' error: %s", command, error.getValStr()));
        }
        // Check if fingerprint is present
        const UniValue& fingerprint = find_value(signer, "fingerprint");
        if (fingerprint.isNull()) {
            if (ignore_errors) return false;
            throw ExternalSignerException(strprintf("'%s' received invalid response, missing signer fingerprint", command));
        }
        std::string fingerprintStr = fingerprint.get_str();
        // Skip duplicate signer
        bool duplicate = false;
        for (ExternalSigner signer : signers) {
            if (signer.m_fingerprint.compare(fingerprintStr) == 0) duplicate = true;
        }
        if (duplicate) break;
        std::string name = "";
        const UniValue& model_field = find_value(signer, "model");
        if (model_field.isStr() && model_field.getValStr() != "") {
            name += model_field.getValStr();
        }
        signers.push_back(ExternalSigner(command, fingerprintStr, mainnet, name));
    }
    return true;
}

UniValue ExternalSigner::getDescriptors(int account)
{
    return RunCommandParseJSON(m_command + " --fingerprint \"" + m_fingerprint + "\"" + (m_mainnet ? "" : " --testnet ") + " getdescriptors --account " + strprintf("%d", account));
}

#endif
