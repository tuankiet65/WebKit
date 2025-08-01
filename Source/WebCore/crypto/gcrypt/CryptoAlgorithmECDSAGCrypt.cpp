/*
 * Copyright (C) 2017 Apple Inc. All rights reserved.
 * Copyright (C) 2017 Metrological Group B.V.
 * Copyright (C) 2017 Igalia S.L.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "CryptoAlgorithmECDSA.h"

#include "CryptoAlgorithmEcdsaParams.h"
#include "CryptoKeyEC.h"
#include "ExceptionOr.h"
#include "GCryptUtilities.h"
#include <pal/crypto/CryptoDigest.h>

namespace WebCore {

static bool extractECDSASignatureInteger(Vector<uint8_t>& signature, gcry_sexp_t signatureSexp, ASCIILiteral integerName, size_t keySizeInBytes)
{
    // Retrieve byte data of the specified integer.
    PAL::GCrypt::Handle<gcry_sexp_t> integerSexp(gcry_sexp_find_token(signatureSexp, integerName, 0));
    if (!integerSexp)
        return false;

    auto integerData = mpiData(integerSexp);
    if (!integerData)
        return false;

    size_t dataSize = integerData->size();
    if (dataSize >= keySizeInBytes) {
        // Append the last `keySizeInBytes` bytes of the data Vector, if available.
        signature.append(integerData->subspan(dataSize - keySizeInBytes, keySizeInBytes));
    } else {
        // If not, prefix the binary data with zero bytes.
        for (size_t paddingSize = keySizeInBytes - dataSize; paddingSize > 0; --paddingSize)
            signature.append(0x00);
        signature.appendVector(*integerData);
    }

    return true;
}

static std::optional<Vector<uint8_t>> gcryptSign(gcry_sexp_t keySexp, const Vector<uint8_t>& data, CryptoAlgorithmIdentifier hashAlgorithmIdentifier, size_t keySizeInBytes)
{
    // Perform digest operation with the specified algorithm on the given data.
    Vector<uint8_t> dataHash;
    {
        auto digestAlgorithm = hashCryptoDigestAlgorithm(hashAlgorithmIdentifier);
        if (!digestAlgorithm)
            return std::nullopt;

        auto digest = PAL::CryptoDigest::create(*digestAlgorithm);
        if (!digest)
            return std::nullopt;

        digest->addBytes(data);
        dataHash = digest->computeHash();
    }

    // Construct the data s-expression that contains raw hashed data.
    PAL::GCrypt::Handle<gcry_sexp_t> dataSexp;
    {
        auto shaAlgorithm = hashAlgorithmName(hashAlgorithmIdentifier);
        if (shaAlgorithm.isNull())
            return std::nullopt;

        gcry_error_t error = gcry_sexp_build(&dataSexp, nullptr, "(data(flags raw)(hash %s %b))",
            shaAlgorithm.characters(), dataHash.size(), dataHash.span().data());
        if (error != GPG_ERR_NO_ERROR) {
            PAL::GCrypt::logError(error);
            return std::nullopt;
        }
    }

    // Perform the PK signing, retrieving a sig-val s-expression of the following form:
    // (sig-val
    //   (dsa
    //     (r r-mpi)
    //     (s s-mpi)))
    PAL::GCrypt::Handle<gcry_sexp_t> signatureSexp;
    gcry_error_t error = gcry_pk_sign(&signatureSexp, dataSexp, keySexp);
    if (error != GPG_ERR_NO_ERROR) {
        PAL::GCrypt::logError(error);
        return std::nullopt;
    }

    // Retrieve MPI data of the resulting r and s integers. They are concatenated into
    // a single buffer, properly accounting for integers that don't match the key in size.
    Vector<uint8_t> signature;
    signature.reserveInitialCapacity(keySizeInBytes * 2);

    if (!extractECDSASignatureInteger(signature, signatureSexp, "r"_s, keySizeInBytes)
        || !extractECDSASignatureInteger(signature, signatureSexp, "s"_s, keySizeInBytes))
        return std::nullopt;

    return signature;
}

static std::optional<bool> gcryptVerify(gcry_sexp_t keySexp, const Vector<uint8_t>& signature, const Vector<uint8_t>& data, CryptoAlgorithmIdentifier hashAlgorithmIdentifier, size_t keySizeInBytes)
{
    // Bail if the signature size isn't double the key size (i.e. concatenated r and s components).
    if (signature.size() != keySizeInBytes * 2)
        return false;

    // Perform digest operation with the specified algorithm on the given data.
    Vector<uint8_t> dataHash;
    {
        auto digestAlgorithm = hashCryptoDigestAlgorithm(hashAlgorithmIdentifier);
        if (!digestAlgorithm)
            return std::nullopt;

        auto digest = PAL::CryptoDigest::create(*digestAlgorithm);
        if (!digest)
            return std::nullopt;

        digest->addBytes(data);
        dataHash = digest->computeHash();
    }

    // Construct the sig-val s-expression, extracting the r and s components from the signature vector.
    PAL::GCrypt::Handle<gcry_sexp_t> signatureSexp;
    gcry_error_t error = gcry_sexp_build(&signatureSexp, nullptr, "(sig-val(ecdsa(r %b)(s %b)))",
        keySizeInBytes, signature.span().data(), keySizeInBytes, signature.subspan(keySizeInBytes).data());
    if (error != GPG_ERR_NO_ERROR) {
        PAL::GCrypt::logError(error);
        return std::nullopt;
    }

    // Construct the data s-expression that contains raw hashed data.
    PAL::GCrypt::Handle<gcry_sexp_t> dataSexp;
    {
        auto shaAlgorithm = hashAlgorithmName(hashAlgorithmIdentifier);
        if (shaAlgorithm.isNull())
            return std::nullopt;

        error = gcry_sexp_build(&dataSexp, nullptr, "(data(flags raw)(hash %s %b))",
            shaAlgorithm.characters(), dataHash.size(), dataHash.span().data());
        if (error != GPG_ERR_NO_ERROR) {
            PAL::GCrypt::logError(error);
            return std::nullopt;
        }
    }

    // Perform the PK verification. We report success if there's no error returned, or
    // a failure in any other case. OperationError should not be returned at this point,
    // avoiding spilling information about the exact cause of verification failure.
    error = gcry_pk_verify(signatureSexp, dataSexp, keySexp);
    return { error == GPG_ERR_NO_ERROR };
}

ExceptionOr<Vector<uint8_t>> CryptoAlgorithmECDSA::platformSign(const CryptoAlgorithmEcdsaParams& parameters, const CryptoKeyEC& key, const Vector<uint8_t>& data)
{
    auto output = gcryptSign(key.platformKey().get(), data, parameters.hashIdentifier, (key.keySizeInBits() + 7) / 8);
    if (!output)
        return Exception { ExceptionCode::OperationError };
    return WTFMove(*output);
}

ExceptionOr<bool> CryptoAlgorithmECDSA::platformVerify(const CryptoAlgorithmEcdsaParams& parameters, const CryptoKeyEC& key, const Vector<uint8_t>& signature, const Vector<uint8_t>& data)
{
    auto output = gcryptVerify(key.platformKey().get(), signature, data, parameters.hashIdentifier, (key.keySizeInBits() + 7)/ 8);
    if (!output)
        return Exception { ExceptionCode::OperationError };
    return WTFMove(*output);
}

} // namespace WebCore
