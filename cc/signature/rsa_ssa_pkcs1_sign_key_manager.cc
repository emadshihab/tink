// Copyright 2018 Google Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
///////////////////////////////////////////////////////////////////////////////

#include "tink/signature/rsa_ssa_pkcs1_sign_key_manager.h"

#include "absl/memory/memory.h"
#include "absl/strings/string_view.h"
#include "tink/public_key_sign.h"
#include "tink/public_key_verify.h"
#include "tink/signature/rsa_ssa_pkcs1_verify_key_manager.h"
#include "tink/signature/sig_util.h"
#include "tink/subtle/rsa_ssa_pkcs1_sign_boringssl.h"
#include "tink/subtle/subtle_util_boringssl.h"
#include "tink/util/enums.h"
#include "tink/util/errors.h"
#include "tink/util/protobuf_helper.h"
#include "tink/util/status.h"
#include "tink/util/statusor.h"
#include "tink/util/validation.h"
#include "proto/rsa_ssa_pkcs1.pb.h"

namespace crypto {
namespace tink {

using crypto::tink::util::Enums;
using crypto::tink::util::Status;
using crypto::tink::util::StatusOr;
using google::crypto::tink::RsaSsaPkcs1KeyFormat;
using google::crypto::tink::RsaSsaPkcs1Params;
using google::crypto::tink::RsaSsaPkcs1PrivateKey;

namespace {
RsaSsaPkcs1PrivateKey RsaPrivateKeySubtleToProto(
    const subtle::SubtleUtilBoringSSL::RsaPrivateKey& private_key) {
  RsaSsaPkcs1PrivateKey key_proto;
  key_proto.set_version(RsaSsaPkcs1SignKeyManager().get_version());
  key_proto.set_d(private_key.d);
  key_proto.set_p(private_key.p);
  key_proto.set_q(private_key.q);
  key_proto.set_dp(private_key.dp);
  key_proto.set_dq(private_key.dq);
  key_proto.set_crt(private_key.crt);
  auto* public_key_proto = key_proto.mutable_public_key();
  public_key_proto->set_version(RsaSsaPkcs1SignKeyManager().get_version());
  public_key_proto->set_n(private_key.n);
  public_key_proto->set_e(private_key.e);
  return key_proto;
}

subtle::SubtleUtilBoringSSL::RsaPrivateKey RsaPrivateKeyProtoToSubtle(
    const RsaSsaPkcs1PrivateKey& key_proto) {
  subtle::SubtleUtilBoringSSL::RsaPrivateKey key;
  key.n = key_proto.public_key().n();
  key.e = key_proto.public_key().e();
  key.d = key_proto.d();
  key.p = key_proto.p();
  key.q = key_proto.q();
  key.dp = key_proto.dp();
  key.dq = key_proto.dq();
  key.crt = key_proto.crt();
  return key;
}

}  // namespace

StatusOr<RsaSsaPkcs1PrivateKey> RsaSsaPkcs1SignKeyManager::CreateKey(
    const RsaSsaPkcs1KeyFormat& rsa_ssa_pkcs1_key_format) const {
  auto e = subtle::SubtleUtilBoringSSL::str2bn(
      rsa_ssa_pkcs1_key_format.public_exponent());
  if (!e.ok()) return e.status();

  subtle::SubtleUtilBoringSSL::RsaPrivateKey private_key;
  subtle::SubtleUtilBoringSSL::RsaPublicKey public_key;
  util::Status status = subtle::SubtleUtilBoringSSL::GetNewRsaKeyPair(
      rsa_ssa_pkcs1_key_format.modulus_size_in_bits(), e.ValueOrDie().get(),
      &private_key, &public_key);
  if (!status.ok()) return status;

  RsaSsaPkcs1PrivateKey key_proto = RsaPrivateKeySubtleToProto(private_key);
  auto* public_key_proto = key_proto.mutable_public_key();
  *public_key_proto->mutable_params() = rsa_ssa_pkcs1_key_format.params();

  return key_proto;
}

StatusOr<std::unique_ptr<PublicKeySign>>
RsaSsaPkcs1SignKeyManager::PublicKeySignFactory::Create(
    const RsaSsaPkcs1PrivateKey& private_key) const {
  auto key = RsaPrivateKeyProtoToSubtle(private_key);
  subtle::SubtleUtilBoringSSL::RsaSsaPkcs1Params params;
  const RsaSsaPkcs1Params& params_proto = private_key.public_key().params();
  params.hash_type = Enums::ProtoToSubtle(params_proto.hash_type());
  auto signer = subtle::RsaSsaPkcs1SignBoringSsl::New(key, params);
  if (!signer.ok()) return signer.status();
  // To check that the key is correct, we sign a test message with private key
  // and verify with public key.
  auto verifier = RsaSsaPkcs1VerifyKeyManager().GetPrimitive<PublicKeyVerify>(
      private_key.public_key());
  if (!verifier.ok()) return verifier.status();
  auto sign_verify_result =
      SignAndVerify(signer.ValueOrDie().get(), verifier.ValueOrDie().get());
  if (!sign_verify_result.ok()) {
    return util::Status(util::error::INTERNAL,
                        "security bug: signing with private key followed by "
                        "verifying with public key failed");
  }
  return signer;
}

Status RsaSsaPkcs1SignKeyManager::ValidateKey(
    const RsaSsaPkcs1PrivateKey& key) const {
  Status status = ValidateVersion(key.version(), get_version());
  if (!status.ok()) return status;
  return RsaSsaPkcs1VerifyKeyManager().ValidateKey(key.public_key());
}

Status RsaSsaPkcs1SignKeyManager::ValidateKeyFormat(
    const RsaSsaPkcs1KeyFormat& key_format) const {
  auto modulus_status = subtle::SubtleUtilBoringSSL::ValidateRsaModulusSize(
      key_format.modulus_size_in_bits());
  if (!modulus_status.ok()) return modulus_status;
  return RsaSsaPkcs1VerifyKeyManager().ValidateParams(key_format.params());
}

}  // namespace tink
}  // namespace crypto

