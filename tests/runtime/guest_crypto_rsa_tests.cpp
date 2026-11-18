#include <array>
#include <thread>
#include <string>
#include <string_view>
#include <vector>

#include <doctest/doctest.h>

#include "../dexvm/tls_fixtures.h"
#include "native_library_fixture.h"
#include "ogplay/hal/clock.h"
#include "ogplay/runtime/integration/dexvm_bridge.h"
#include "ogplay/session/android_app_process.h"

namespace {
using namespace ogplay;
using namespace ogplay::runtime::dexvm;
using namespace ogplay::test::native_library;

constexpr auto kBackends = {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded};
constexpr auto kFactory = "Ljava/security/KeyFactory;";
constexpr auto kGenerator = "Ljava/security/KeyPairGenerator;";
constexpr auto kCipher = "Ljavax/crypto/Cipher;";
constexpr auto kSignature = "Ljava/security/Signature;";
constexpr auto kParameters = "Ljava/security/AlgorithmParameters;";
constexpr auto kNative = "Lcom/android/org/conscrypt/NativeCrypto;";
constexpr auto kPublicSpec = "Ljava/security/spec/RSAPublicKeySpec;";
constexpr auto kPrivateSpec = "Ljava/security/spec/RSAPrivateKeySpec;";
constexpr auto kCrtSpec = "Ljava/security/spec/RSAPrivateCrtKeySpec;";
constexpr auto kX509Spec = "Ljava/security/spec/X509EncodedKeySpec;";
constexpr auto kPkcs8Spec = "Ljava/security/spec/PKCS8EncodedKeySpec;";
constexpr auto kGetSpec = "(Ljava/security/Key;Ljava/lang/Class;)Ljava/security/spec/KeySpec;";
constexpr auto kRsaPrivate = "Lcom/android/org/conscrypt/OpenSSLRSAPrivateCrtKey;";
constexpr auto kRsaPublic = "Lcom/android/org/conscrypt/OpenSSLRSAPublicKey;";
constexpr auto kConscrypt = "AndroidOpenSSL";

// Fixed test-only key and oracle vectors generated with host OpenSSL 3.6:
// openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:2048 -out rsa.pem
// openssl pkey -in rsa.pem -pubout -outform DER (PEM rsa.pub for pkeyutl)
// openssl pkcs8 -in rsa.pem -topk8 -nocrypt -outform DER
// openssl pkey -in rsa.pem -text -noout
// openssl genpkey -algorithm EC -pkeyopt ec_paramgen_curve:P-256 -out ec.pem
// openssl pkeyutl -encrypt -pubin -inkey rsa.pub -pkeyopt rsa_padding_mode:pkcs1
// Repeat with rsa_padding_mode:none and a 256-byte left-zero-padded message.
// openssl dgst -{md5,sha1,sha256,sha384,sha512} -sign rsa.pem
// openssl pkeyutl -sign -inkey rsa.pem -pkeyopt rsa_padding_mode:pkcs1
// openssl pkeyutl -encrypt -pubin -inkey rsa.pub -pkeyopt rsa_padding_mode:oaep
// Add -pkeyopt rsa_oaep_md:<hash> -pkeyopt rsa_mgf1_md:<hash>.
// The mixed SHA-256/SHA-1 case adds -pkeyopt rsa_oaep_label:6c6162656c.
constexpr std::string_view kPublic =
    "30820122300d06092a864886f70d01010105000382010f003082010a0282010100b4bf0fcca8debace5d4f805d4001fda008a55702e0"
    "330c76e4dd4fb22fac774f3bedbed4b18db3a274c30860eb8603aee9978736a3379268d2fb69c73e6d27c5d7c1add410287f6bcf942b"
    "b5a9c10ed9356a7426fb2bee14563eee52bb3a2e05c7113049e6d3a49652bdaa63a41b6d39e282219426bb2e7a4ad0a1fe2b67fb614d"
    "2fb02fd16ce0523c7fb73bb4a21f30bb64b2cbf6d8c4edaf32d1d6d151a32753c76463af70552b14ff42b81ec5d4025de5ceb8913d72"
    "2df2761377cb5799666f90d6bb485ac7ffef98fff5e128f7616475190fdece5b01933c0b1b19442a9458500d3c547567033956698351"
    "eeaeb68fb2a95132a6acda4e18cff0767d3c2f0203010001";
constexpr std::string_view kPrivate =
    "308204bd020100300d06092a864886f70d0101010500048204a7308204a30201000282010100b4bf0fcca8debace5d4f805d4001fda0"
    "08a55702e0330c76e4dd4fb22fac774f3bedbed4b18db3a274c30860eb8603aee9978736a3379268d2fb69c73e6d27c5d7c1add41028"
    "7f6bcf942bb5a9c10ed9356a7426fb2bee14563eee52bb3a2e05c7113049e6d3a49652bdaa63a41b6d39e282219426bb2e7a4ad0a1fe"
    "2b67fb614d2fb02fd16ce0523c7fb73bb4a21f30bb64b2cbf6d8c4edaf32d1d6d151a32753c76463af70552b14ff42b81ec5d4025de5"
    "ceb8913d722df2761377cb5799666f90d6bb485ac7ffef98fff5e128f7616475190fdece5b01933c0b1b19442a9458500d3c54756703"
    "3956698351eeaeb68fb2a95132a6acda4e18cff0767d3c2f0203010001028201001b3682ff4a28e7fb9c62950ddfce6241bf30d44610"
    "bc6432ddbd6666350af9ec4f8bb986161f7812cc67f587261ec7722fd04a76d2f72724d4d32e8cbadce8b6945d4ae483f447a4534758"
    "976187642016e1eabaadbeb7a48f370009d664e990a1754de31537a5eef8bbd3ff6c3206ed71542cbb120e6f99265228244cf9105df2"
    "4e860d82577c32be0611ec4b50138f385e28bd483bf51800b7290ecb6002a7dbd538494520758dd22eae2a405535f152202e3c3a96eb"
    "81229e8bd7ea7f8ceb747717a268beb77a4bb291ba1690b34eeb62e80347f04b4fb1389254d1ccd1cda997eb727202e620e2113a0570"
    "a452818352f5645b3be8a776086baabba375d502818100f08cc3075ccc5e470bee075ab4974e3acac089c96db3599be8deec4ac4eb1a"
    "5ed2b894e1995f99e6c304da34819619de506f3de719241174ec9ee9f3d3c327f19afec5281e944f44f267ae48d92606f14f010725fd"
    "f0e3265e081e45a2b06f13550de0a0742703a7fdec6c985baa7e23d870a74591163156ca941c2a529191b502818100c05afb33adeacd"
    "c0bd7bcba38c6b45e71b1b6a8e641516536b2302885febde07176e4f46ee2c825ab8bb5976b3c9026e2eeb30bf63e90c976e117db19e"
    "ec70dc084d8e10d8afb77a296d6bf779e587a961bd4d22d0ac78372d2734e2892f362baefac3d270578459c2d9cd7db4749061db9d9b"
    "ca1acbf8db0994e87b18a014d302818100d32a0e4ed944c28245d3b77b05cb492e4d2c4608a01770537052b9454da64128fa7c749db2"
    "60fd1e59165a62fb5b03ec67d9af92be3c2e62e11103b93aad37d45d46c75657b9c3f8777f52bcba7b2a5f0d068efae7f85dbf218ea8"
    "dc625bd5b637d50dda6cfc8eb61b2f3f8963f6bff2c19eeb0823bc42a5b901d9d3c155ee7d0281803d08e16a5d8602b164ae72483bd7"
    "120029d6c231da49e2a2a77d1d619e661e364bd601abb50944777ae45f97cda63b7230cb09d5c0e302cecdeb18e212565ddd43793059"
    "e217a77548cde2934d076acb712afaddd02ed99a50667f5e5e3f2fc61ea88d1829f36b187c3e55f8cc18ac739096eb06ef07c7b7e0cb"
    "721515143e670281800fa9d9ebc775ef23c79b52926351635a1f2fb009a1aad7da587bf7ccd346b8b8208afda36df94b2845e72b75d9"
    "8eb125d985adbe6026a33971ed7931709d35f9850bff13e7b3afee52d53cfd468df59d1d0c0133c552413a4134f88d68e945b4662054"
    "7d6999bd1b1fce6608553a9b09f1407b8d5d8b44d3ca578c9e184ff78b";
constexpr std::string_view kEcPublic =
    "3059301306072a8648ce3d020106082a8648ce3d030107034200042cbe53f907f6fa3bb94262a054f80c5bcce4c9f2e44a5eaf3a7d6e"
    "5e06afa312e5db45bf80a6b9d7100c4b19e8d9545c4945c307f956ba58cc9df75eea596061";
constexpr std::string_view kEcPrivate =
    "308187020100301306072a8648ce3d020106082a8648ce3d030107046d306b020101042098a1c2342fb27fdb79f1394b13c7b65377aa"
    "2e214162015a45a74970fc973082a144034200042cbe53f907f6fa3bb94262a054f80c5bcce4c9f2e44a5eaf3a7d6e5e06afa312e5db"
    "45bf80a6b9d7100c4b19e8d9545c4945c307f956ba58cc9df75eea596061";
constexpr std::string_view kMessage = "4f47506c61792052534120696e7465726f7065726162696c6974790a";
constexpr std::string_view kPkcs1 =
    "010bd3b19a6d755e7fcd5f77d92959979fefc1d9e1a38f85de855679718ea111bb94b3cea43b0fe5490905953468f23630b9979702a3"
    "4fe1a0c5266f9f91b68c8c938b803f368b29b5f12a3161715b3ec14a8b8273fd14876ba18090f542cf9a85960e73c933bdcd0b253ee0"
    "7478538c9a1fc189dbf2f5b8d922bf5438583f18cc666c2e230406b523aa139277c43c64b1507728b86e57196ec941cd9b7681e6efa4"
    "436b5a573920c2e98c92be85475f9cf746baaf12c608edf2620e2ce9a1e52517dfaa7e0cfac83d5744ab8b92e169f71de3a4b0410eaf"
    "67dea1555498b4679d18fd383febf70e783cede4b0091adec8b315df2246c1cf9e02cc73c38f9371";
constexpr std::string_view kRaw =
    "718f7f01cddf41f8acd7e38e063354f221c37c97ec76b9b8e618123cbc893b839b61f7be3a7ac7a576ca34d70aa9ad711c9082e02291"
    "fbc4acd0f838ee1e00575071ba485853e2a53347969fe44ef2e164e8ee8deb25d5f500e092ea7d4819542de3b6ae1ce3ee3e63233bf4"
    "8e52626909bc439da2c25a7a7974c1cc0fb111af893652fa723603887f7ca9367ea7d347759206c492b6cef9a1c7f5334adfbc853fba"
    "831163d39b478ca93f56d5f87f0066c716717968c2834d28bb30b763583c176055de6db1d82c73b10cec80476fcb95520d9232897878"
    "08b081e68584204f853660b8e55a3a933e4b5beeef298b8e9a1c1e409bf60439a214a34a0b912360";
constexpr std::string_view kRawSignature =
    "1554bd00aebb79ff76262b534457ea196098a36b17f0fb016e2027e150e92ff73dec2e0d47adc15c97e1f9892ff2fd2afbee68fe926d"
    "0d95e2b84f17415930a74042947c56d9301d3d2b59b209cbdb3b3cf2e4331beb46f8048c2c42ca2e24a9403ca123eedd3fde3a963789"
    "35c4071fbf50dfa13ca4ce852aeba98f12cf787c7d97ab57f9de26f23f258df1a061d798d8be9589f39fc55a8e526080d22c253ec8e1"
    "1bc8ffbac0e5289d7fd4dcf537379a4e52db8d0aa92b156a09289ca6d3159e09b7fc69f392801cd8f0af9e2a6ebd71589564c135d7f5"
    "7d340b26af044939f33238819687e3ac93b7b07b358782489d094bc7df8b91c87c487115ba4ac14d";
constexpr std::string_view kSignatureMD5 =
    "03a1bcb2f9dd5dff50ccf7a55128b37c3d9f76a213cb46ce675b96ee9967c2945e5a6d20272344460bbb627e4f37df9d3962f6e7a6db"
    "c7b4d5e89df8a4b70adbf379e37cfec892b4ccc88ae1e3522653ad6af439de3675e5975e3095a5738f12a365689606430f196be7f44f"
    "851bb74dfd91e5d5e742d7a1877249c5b0695c49945ce53d36a7b4d1a2e9cb4d9b13e7310ca6936d89a52dc75738e5a73b381a2be241"
    "bf1ad9d24d64a674c7187b68f50425ead9884ae1fbae7563499c4d67eb8ef6589f368c9dcb0f227e5a45eba06e274d236395dba1907b"
    "84d1d8ff13650eadeeb57f4b099085a963e500379e240483cf54f4ea0e2aae577f41b61e2a87648e";
constexpr std::string_view kSignatureSHA1 =
    "0198a38f6998fb90a673f5ea463ef798577b5daa5384e3d1b44317280ec4f29548e46c52080cbf179a5f299e046ffec335650e00493e"
    "5a91292a8653b852327b2f6e4a494eca42edc6f174c3641700fdf56cd6828dd121223420ab960e4781e1325d54fe40a1c06c3bf904f0"
    "a2bd1a1c5bb1a9c5a7b2713de426b4891f521fa303f35baae73fa807733e9fc419f29588bb8fe1a294fd36a51263b76e8e0db41c2b6b"
    "34ffddb0dc84bd444b0328b144d60ddf1b782d36cac6d349bb00d44f14ae6a878fb06e84b90ca74d9a73345d63cd2b4c7f7e8f4a055b"
    "328ba600ce8a84cb2bd457f7d7fd0a32c219b7e288808f4f7af0385ce801e3d8395544e2de1d355e";
constexpr std::string_view kSignatureSHA256 =
    "306c425a63587b1a4c2e3f29cf1da6f7dd6bf9764880ceaa3df0322b50bedca8cfd36dc1315cd9a8f83126a5f73a6017bc4414dd3673"
    "278b343b43ce11648362b26eca7307ac80141bdfebebf1fc3ad86b7ec5b515b1a62023b8f8e5d66fc8f80d2a262a91db6f0482afa150"
    "2eeaa0f93d1fc576178a54b8984eb074c4f603369b0e9018901c0ae02842a9287810f7fb43965a8f97df3b669569b18966f54fe8afe4"
    "b74a91acbdcc60bdeed06481931c49b284843198e11044dab3bba9e6ec896706cafb18a43d919a887d7aae781f795307dc5d28bc00f3"
    "b85bd4ef978a9fd3e8442fcd37ceeb4888d4e292d08a695c4f82140c908c14b8de3fdd98f7887e0d";
constexpr std::string_view kSignatureSHA384 =
    "7bcb6178f7f146a5e94b147facfddf0d3764c2ef84218e6b6669849aa96363ae0544148438d3fc167d455e6c7f52e5edc77595f30f32"
    "88a9ea56f185d1e30cfd172a244c8568adee396e96400176bc5c836f9c1f28cc5228363389af0bdbb7d226421f3c53eb530fc66bdd5a"
    "2ec67438c240f1a8f1debae88c18839067ca87fa0a8eb572d1703d0bba29155ab705c695f15ef79cb827bb94e13ee95c633c5d9a3e6b"
    "83af7d319cc15bd084d239c8bf191105dc7356d683edc6d2914a9c4e57042b74317ec38f575027d924c599fa9625e84e2617fcaab15c"
    "20c5d64d8df423e6ca24e5b88369b35537023cd44ec50abeab6efc6402aa6d4c1486852d94a5f83c";
constexpr std::string_view kSignatureSHA512 =
    "adc3741162e061e7a6b7655e3ae1cd2ce73ad4ac97d6b965e8bfe6aaf1e42b115e94858268ce9261dc306d55ebaeb81d87d132b52e2e"
    "f09a1b00d355907ff546c7bd15fdf95f62a433c97e191417ab876d517e45b3c18e509342507851120a01aba91e4c1cb4693ce449101b"
    "d371345d1d5d2432f4c9ede8ec749cbfa426e05110cbeda097771b868ef5329841300dc0fc5d3f2b402d22aab6b60e50efe5bdbc48d7"
    "32ace607095542bc157bde99e4515b9701328e4f3c5d7583326734b8a44f2c2e3b6e5befce7e2464b17181b877c24683dfa7a4a00e8d"
    "aff45889022cec3a033be708b85536557d4a100c91887eca9a10bd356bd65abb6614576df4e79929";
constexpr std::string_view kOaepSHA1 =
    "99fe360bbb057cf054b99769a05fb89f1ad66c924ac5c3a60deda95cd6bcd421c0b832823e972dfdab80d3744be4d5d95441d53d6d6f"
    "b6baedaa57ad8ea57af89e648e35adbfba7fd05425fb6e9a9d3da6989c4834ad7ea723690ed355d28b892cbdb46d46f7b5de22a3fb68"
    "ccbc54bbbd70ab94bd60625de0e3d699fb709165975faf5a42db5d5575f15f0f3b20703c3588be6211b957cfa07671eaafdfc7daa065"
    "e13f57deb4b52bd5c8f07146e033e2178d18a4649d7939189d07d2bdee36ccc97b41b30c822c7c4f70c4f25366667480b93bdc721baa"
    "ab302715dd1685c4815f3e4d4d6d6e03c26d9dad4aead0cae3bc969cc7567d6a0661620de8419cf5";
constexpr std::string_view kOaepSHA224 =
    "1a4ae66107c92575b70cc29b4644b99b7ac6a39082cabcf946c87ad1a0ae59cae8a20677641c6f32282a5f0f9d78386d1809d7c985e2"
    "a50180242d0ce7218a895473319a8e431224aed92bd66400d1b2d1649d2352490a84df09f5e014a9be83ee73f0d7a127e95b39011dbf"
    "3a18b5368b69aec0405316372590182a1a6e76200ba3d1f925ecf9b9324ec64a463055fdcdb609f0ad9050b26bda26d777fd36571126"
    "9b9079df9080ee98915f42b80d6a5551d4a9011d99eefbf75de076798711c2272a09770cf202ac9e92b97450fbaf2de14c183105b847"
    "f4941fd72b634c84073c880a2338af9d518bf142737d17c24426169c5592102dc809e7688e8a4858";
constexpr std::string_view kOaepSHA256 =
    "6eb444a6e6ad9a63d78419195986f53b109a09ca1fc2bc72c600f0a75be402c65dc05664726298a5dc0fb31b85e7123b8c788abf7f7d"
    "7c09c5d2d42355d3ebb97b605ad2f3bb0e44bd73f90a78345b69b1a73f2803cf1ca68c2a0438a27cb8acf3f9d210521991a656ee750e"
    "8ed08001d2616cb3fee96d7d1c184dbf70bffee22f95720800145f5d852b9e2773528ef3dc4eb74e4988ac69f520be06cb4886070482"
    "608a7fcccbce6a69330497e663c8c5aab95d9b48f2a563df38d119ffced65db72e597c2980999a5ab327d55db03e5dd64ad17a6867e4"
    "fb3ef958cf5a9c503b1b63de14829ac4b72e9b8cddf86c318e27cf38b4446dd257bd20e51eff599c";
constexpr std::string_view kOaepSHA384 =
    "566ad807e9335dfc5f697ff8db5b31c4268515c4dc19a5f838708cf313246e2d3829a770023a93551dcbd9c7260b7d84081cbf187435"
    "5500b1623605e80cd72a4eeb64880490c1401ef19cba345de805f548c8d238c022a19aaf4e9ae1b3c5d345abe6689825af2d714024bb"
    "4a77ea11600fb2e541802e565bc31da3f7deab44d6e79d038231990f46d06344ecda1b31d4a34e46b9dff2cb49115f416a233390d6fb"
    "d61e36b526da6c52a659d937751f1ef680ce875c20f6561492740cdb8322e1fcf22f173959e65e34b02ccb0528e8e533bde8484e2780"
    "ea502b7cd0fed5ab105df803c5ca360d8748eb8923a7b3e60e0329e6dfade7c326f6cf96bcc30f0c";
constexpr std::string_view kOaepSHA512 =
    "259f1075f2393b084e39dee313e04bc75c1c27e0c10d4eb47035e24902e049f34d08132161437894b6faf46cc1dedc21dd35e2be00ed"
    "d110a3cc59ea53573ced39d3360979f9d13280117a9e75fc7609d41dcca9d4872df09d67edd2627defe8d359697772860a50fabf6d97"
    "a67a854b16b44ee844adecf9afdff0941f0c54f86d02af818aeaf535b9b9e6e77968d03d6448a8db965865b7181b74660414db50cbac"
    "8acc27dae897808ca62fa19cd98d103d203f524ae1449b981b312b70be1fa9b5d504b208c23f0d42f6a174a90b999c4a56cbb2a17a35"
    "259844178f65fa9dc9045e960843f4fdfdf40774e30847057ec9e9201448329d1d57658e175ca5d5";
constexpr std::string_view kOaepLabel =
    "a4a4e524504354c87ed5a11e8f9932c66ac82904da8ee6ae40525b51a8c63d11445644563bd30fbc956fb1c3428907126c66a448dcd3"
    "b49527100807a710089796dda3c3ed20834650a0e46ce4faaeb7c75f91650490f9ea559939b67a280d1f0ad31c4d566a961923b69ff7"
    "60e06f2415e573b99670257141f8fef4bea1c6c1be9ea3de5f5e84822973593a1441a97eb40ff39f8dbb694c9c24e914714a74a9f9c8"
    "c8a9db428191b780bfd545645b72548e7ac177b0378364c003b0d86839c9ecfa660308556d7c991b5d31699148a32c8334f72de19cf6"
    "fc0304ae16b3b2c321ef4614b39d386bd814593ce90246a21946c582784bef0c67b542f79c4ceb88";
// SHA-1 OAEP ciphertext whose first byte is zero, stored with that byte removed.
constexpr std::string_view kOaepLeadingZero =
    "722a927c9c7404324b0aad4745b7dda66f35bf45c0283b32536a66f688497820112d6ae428a4feb27e0307dbb78979cc11f8885583eb"
    "876ca272ba535ff7a46da22f115d495e0ed3af94c9e24d7ee79d3128635e3e0c94d57932d4681825f901302b863879a02ebbaa71ead6"
    "75155e2f0b6ac2892438843bc26b8289a881f52376fcde777ddbfa7b1304e758c4c25beb7d689850f0e6616406a6ac988086dde5be90"
    "31dda685f957f95c907d92b9fc555ae8625e2287d4a5eb5d86fdabb3474f85b9a5ed69b18a6ab9c11aad216f791aef92112f255e5c77"
    "c7513b888434e11056697feda32add43c5370a89e4cde786e5790cef656fa2d63afac623bf0fd6";
constexpr std::string_view kModulus =
    "00b4bf0fcca8debace5d4f805d4001fda008a55702e0330c76e4dd4fb22fac774f3bedbed4b18db3a274c30860eb8603aee9978736a3"
    "379268d2fb69c73e6d27c5d7c1add410287f6bcf942bb5a9c10ed9356a7426fb2bee14563eee52bb3a2e05c7113049e6d3a49652bdaa"
    "63a41b6d39e282219426bb2e7a4ad0a1fe2b67fb614d2fb02fd16ce0523c7fb73bb4a21f30bb64b2cbf6d8c4edaf32d1d6d151a32753"
    "c76463af70552b14ff42b81ec5d4025de5ceb8913d722df2761377cb5799666f90d6bb485ac7ffef98fff5e128f7616475190fdece5b"
    "01933c0b1b19442a9458500d3c547567033956698351eeaeb68fb2a95132a6acda4e18cff0767d3c2f";
constexpr std::string_view kPrivateExponent =
    "1b3682ff4a28e7fb9c62950ddfce6241bf30d44610bc6432ddbd6666350af9ec4f8bb986161f7812cc67f587261ec7722fd04a76d2f7"
    "2724d4d32e8cbadce8b6945d4ae483f447a4534758976187642016e1eabaadbeb7a48f370009d664e990a1754de31537a5eef8bbd3ff"
    "6c3206ed71542cbb120e6f99265228244cf9105df24e860d82577c32be0611ec4b50138f385e28bd483bf51800b7290ecb6002a7dbd5"
    "38494520758dd22eae2a405535f152202e3c3a96eb81229e8bd7ea7f8ceb747717a268beb77a4bb291ba1690b34eeb62e80347f04b4f"
    "b1389254d1ccd1cda997eb727202e620e2113a0570a452818352f5645b3be8a776086baabba375d5";
constexpr std::string_view kPrime1 =
    "00f08cc3075ccc5e470bee075ab4974e3acac089c96db3599be8deec4ac4eb1a5ed2b894e1995f99e6c304da34819619de506f3de719"
    "241174ec9ee9f3d3c327f19afec5281e944f44f267ae48d92606f14f010725fdf0e3265e081e45a2b06f13550de0a0742703a7fdec6c"
    "985baa7e23d870a74591163156ca941c2a529191b5";
constexpr std::string_view kPrime2 =
    "00c05afb33adeacdc0bd7bcba38c6b45e71b1b6a8e641516536b2302885febde07176e4f46ee2c825ab8bb5976b3c9026e2eeb30bf63"
    "e90c976e117db19eec70dc084d8e10d8afb77a296d6bf779e587a961bd4d22d0ac78372d2734e2892f362baefac3d270578459c2d9cd"
    "7db4749061db9d9bca1acbf8db0994e87b18a014d3";
constexpr std::string_view kExponent1 =
    "00d32a0e4ed944c28245d3b77b05cb492e4d2c4608a01770537052b9454da64128fa7c749db260fd1e59165a62fb5b03ec67d9af92be"
    "3c2e62e11103b93aad37d45d46c75657b9c3f8777f52bcba7b2a5f0d068efae7f85dbf218ea8dc625bd5b637d50dda6cfc8eb61b2f3f"
    "8963f6bff2c19eeb0823bc42a5b901d9d3c155ee7d";
constexpr std::string_view kExponent2 =
    "3d08e16a5d8602b164ae72483bd7120029d6c231da49e2a2a77d1d619e661e364bd601abb50944777ae45f97cda63b7230cb09d5c0e3"
    "02cecdeb18e212565ddd43793059e217a77548cde2934d076acb712afaddd02ed99a50667f5e5e3f2fc61ea88d1829f36b187c3e55f8"
    "cc18ac739096eb06ef07c7b7e0cb721515143e67";
constexpr std::string_view kCoefficient =
    "0fa9d9ebc775ef23c79b52926351635a1f2fb009a1aad7da587bf7ccd346b8b8208afda36df94b2845e72b75d98eb125d985adbe6026"
    "a33971ed7931709d35f9850bff13e7b3afee52d53cfd468df59d1d0c0133c552413a4134f88d68e945b46620547d6999bd1b1fce6608"
    "553a9b09f1407b8d5d8b44d3ca578c9e184ff78b";

std::vector<std::byte> Hex(std::string_view hex) {
    std::vector<std::byte> bytes;
    for (std::size_t i = 0; i < hex.size(); i += 2)
        bytes.push_back(static_cast<std::byte>(std::stoul(std::string(hex.substr(i, 2)), nullptr, 16)));
    return bytes;
}

class RsaFixture {
    OrchestratedApp app_;
    std::vector<Interpreter::RootScope> roots_;

    VmValue Keep(VmValue value) {
        if (value.kind == VmValue::Kind::ref && value.ref.IsValid())
            roots_.push_back(vm.ProtectReferences(std::array{value.ref}));
        return value;
    }

public:
    explicit RsaFixture(InterpreterBackend backend)
        : app_("fixture.LauncherActivity", true, false, {}, {}, true, backend, "android.app.Application", {}, {64, 36},
               {}, false, {}, runtime::kDefaultMaximumTicksPerCall),
          vm(app_.app->DexVm().Vm()), linker(vm.Linker()) {
        app_.app->StartApplication();
    }

    void Run(const std::function<void()>& test) {
        app_.RunOnMainThread([&] {
            try {
                test();
            } catch (...) {
                DropRoots();
                throw;
            }
            DropRoots();
        });
    }

    Interpreter& vm;
    DexClassLinker& linker;

    VmCallOutcome TryStatic(const char* owner, const char* name, const char* desc,
                            const std::vector<VmValue>& args = {}) {
        auto type = linker.ResolveDescriptor(owner);
        auto method = linker.FindDirectMethod(type, name, desc);
        REQUIRE_MESSAGE(method, owner << " " << name << desc);
        return vm.Call(*method, args);
    }

    VmCallOutcome TryVirtual(VmValue object, const char* name, const char* desc, std::vector<VmValue> args = {}) {
        auto type = vm.Model().ObjectClass(object.ref);
        auto slot = linker.FindVtableIndex(type, name, desc);
        REQUIRE_MESSAGE(slot, linker.Class(type).descriptor << " " << name << desc);
        args.insert(args.begin(), object);
        return vm.Call(linker.Class(type).vtable[*slot], args);
    }

    VmValue Value(VmCallOutcome result) {
        if (result.exception.IsValid()) {
            FAIL(linker.Class(result.exception_class).descriptor << " " << result.exception_message);
        }
        return Keep(result.value);
    }

    VmValue Static(const char* owner, const char* name, const char* desc, std::vector<VmValue> args = {}) {
        return Value(TryStatic(owner, name, desc, args));
    }

    VmValue Call(VmValue object, const char* name, const char* desc, std::vector<VmValue> args = {}) {
        return Value(TryVirtual(object, name, desc, std::move(args)));
    }

    void Exception(const VmCallOutcome& result, std::string_view expected) {
        REQUIRE_MESSAGE(result.exception.IsValid(), expected);
        CHECK(linker.Class(result.exception_class).descriptor == expected);
    }

    VmValue Construct(const char* owner, const char* desc, std::vector<VmValue> args) {
        auto object = Keep(VmValue::Ref(vm.NewIntrinsicInstance(owner)));
        args.insert(args.begin(), object);
        Static(owner, "<init>", desc, std::move(args));
        return object;
    }

    VmValue String(std::string_view text) { return Keep(VmValue::Ref(vm.NewStringUtf8(std::string(text)))); }

    std::string Text(VmValue value) { return vm.StringUtf8(value.ref); }

    VmValue Bytes(std::span<const std::byte> bytes) {
        auto array = vm.Model().NewPrimitiveArray(linker.ResolveDescriptor("[B"), runtime::JniPrimitiveKind::byte,
                                                  static_cast<runtime::JniSize>(bytes.size()));
        vm.Model().WriteByteRegion(array, 0, bytes);
        return Keep(VmValue::Ref(array));
    }

    VmValue Bytes(std::string_view hex) { return Bytes(Hex(hex)); }

    VmValue Chars(std::string_view text) {
        auto array = vm.Model().NewPrimitiveArray(linker.ResolveDescriptor("[C"), runtime::JniPrimitiveKind::character,
                                                  static_cast<runtime::JniSize>(text.size()));
        for (std::size_t i = 0; i < text.size(); ++i)
            vm.Model().SetPrimitiveElement(array, static_cast<runtime::JniSize>(i),
                                           static_cast<unsigned char>(text[i]));
        return Keep(VmValue::Ref(array));
    }

    std::vector<std::byte> Read(VmValue array) {
        return vm.Model().ReadByteRegion(array.ref, 0, vm.Model().ArrayLength(array.ref));
    }

    std::string Class(VmValue value) { return linker.Class(vm.Model().ObjectClass(value.ref)).descriptor; }

    std::string Provider(VmValue jca) {
        return Text(Call(Call(jca, "getProvider", "()Ljava/security/Provider;"), "getName", "()Ljava/lang/String;"));
    }

    VmValue Factory(std::string_view name = "RSA") {
        return Static(kFactory, "getInstance", "(Ljava/lang/String;)Ljava/security/KeyFactory;", {String(name)});
    }

    VmValue Cipher(std::string_view name) {
        return Static(kCipher, "getInstance", "(Ljava/lang/String;)Ljavax/crypto/Cipher;", {String(name)});
    }

    VmValue Signature(std::string_view name) {
        return Static(kSignature, "getInstance", "(Ljava/lang/String;)Ljava/security/Signature;", {String(name)});
    }

    VmValue Generate(VmValue factory, VmValue spec, bool private_key) {
        return private_key
                   ? Call(factory, "generatePrivate", "(Ljava/security/spec/KeySpec;)Ljava/security/PrivateKey;",
                          {spec})
                   : Call(factory, "generatePublic", "(Ljava/security/spec/KeySpec;)Ljava/security/PublicKey;", {spec});
    }

    VmValue Spec(VmValue factory, VmValue key, const char* type) {
        auto cls = vm.Model().ClassObject(linker.ResolveDescriptor(type));
        return Call(factory, "getKeySpec", kGetSpec, {key, VmValue::Ref(cls)});
    }

    VmValue BigInt(std::string_view hex) {
        return Construct("Ljava/math/BigInteger;", "(I[B)V", {VmValue::Int(1), Bytes(hex)});
    }

    VmValue PublicKey(VmValue factory) {
        return Generate(factory, Construct(kX509Spec, "([B)V", {Bytes(kPublic)}), false);
    }

    VmValue PrivateKey(VmValue factory) {
        return Generate(factory, Construct(kPkcs8Spec, "([B)V", {Bytes(kPrivate)}), true);
    }

    void CipherInit(VmValue cipher, int mode, VmValue key) {
        Call(cipher, "init", "(ILjava/security/Key;)V", {VmValue::Int(mode), key});
    }

    VmValue Final(VmValue cipher, VmValue input) { return Call(cipher, "doFinal", "([B)[B", {input}); }

    VmValue Sign(VmValue signature, VmValue private_key, VmValue data) {
        Call(signature, "initSign", "(Ljava/security/PrivateKey;)V", {private_key});
        Call(signature, "update", "([B)V", {data});
        return Call(signature, "sign", "()[B");
    }

    int Verify(VmValue signature, VmValue public_key, VmValue data, VmValue signed_data) {
        Call(signature, "initVerify", "(Ljava/security/PublicKey;)V", {public_key});
        Call(signature, "update", "([B)V", {data});
        return Call(signature, "verify", "([B)Z", {signed_data}).AsInt();
    }

    void DropRoots() {
        while (!roots_.empty())
            roots_.pop_back();
    }
};

constexpr auto kBigInt = "()Ljava/math/BigInteger;";
constexpr auto kN = "getModulus";
constexpr auto kE = "getPublicExponent";
constexpr auto kD = "getPrivateExponent";
constexpr auto kEValue = "010001";
const std::array kCrtComponents = {std::pair{kN, kModulus},
                                   std::pair{kE, std::string_view(kEValue)},
                                   std::pair{kD, kPrivateExponent},
                                   std::pair{"getPrimeP", kPrime1},
                                   std::pair{"getPrimeQ", kPrime2},
                                   std::pair{"getPrimeExponentP", kExponent1},
                                   std::pair{"getPrimeExponentQ", kExponent2},
                                   std::pair{"getCrtCoefficient", kCoefficient}};

void CheckComponent(RsaFixture& f, VmValue key_or_spec, const char* member, std::string_view expected) {
    auto value = f.Call(key_or_spec, member, kBigInt);
    CHECK(f.Call(value, "equals", "(Ljava/lang/Object;)Z", {f.BigInt(expected)}).AsInt() == 1);
}

void CheckEncoding(RsaFixture& f, VmValue key, std::string_view expected) {
    CHECK(f.Read(f.Call(key, "getEncoded", "()[B")) == Hex(expected));
}

void CheckEqual(RsaFixture& f, VmValue a, VmValue b) {
    CHECK(f.Call(a, "equals", "(Ljava/lang/Object;)Z", {b}).AsInt() == 1);
    CHECK(f.Call(b, "equals", "(Ljava/lang/Object;)Z", {a}).AsInt() == 1);
    CHECK(f.Call(a, "hashCode", "()I").AsInt() == f.Call(b, "hashCode", "()I").AsInt());
}
}  // namespace

TEST_CASE("API19 RSA KeyFactory round-trips encoded and mathematical key specs") {
    for (auto backend : kBackends) {
        CAPTURE(std::string_view(backend == InterpreterBackend::threaded ? "threaded" : "switch"));
        RsaFixture f(backend);
        f.Run([&] {
            auto factory = f.Factory("1.2.840.113549.1.1.1");
            CHECK(f.Provider(factory) == kConscrypt);
            auto public_key = f.PublicKey(factory);
            auto private_key = f.PrivateKey(factory);
            CHECK(f.Class(public_key) == kRsaPublic);
            CHECK(f.Class(private_key) == "Lcom/android/org/conscrypt/OpenSSLRSAPrivateKey;");
            CheckEncoding(f, public_key, kPublic);
            CheckEncoding(f, private_key, kPrivate);
            CheckComponent(f, private_key, kN, kModulus);
            CheckComponent(f, private_key, kD, kPrivateExponent);

            auto x509 = f.Spec(factory, public_key, kX509Spec);
            auto pkcs8 = f.Spec(factory, private_key, kPkcs8Spec);
            CheckEncoding(f, x509, kPublic);
            CheckEncoding(f, pkcs8, kPrivate);
            CheckEqual(f, public_key, f.Generate(factory, x509, false));
            CheckEqual(f, private_key, f.Generate(factory, pkcs8, true));

            auto pub_spec = f.Spec(factory, public_key, kPublicSpec);
            CheckComponent(f, pub_spec, kN, kModulus);
            CheckComponent(f, pub_spec, kE, kEValue);
            auto public_from_numbers = f.Generate(factory, pub_spec, false);
            CheckEqual(f, public_key, public_from_numbers);
            CheckEncoding(f, public_from_numbers, kPublic);

            std::vector<VmValue> crt_fields;
            for (const auto& [member, value] : kCrtComponents)
                crt_fields.push_back(f.BigInt(value));
            constexpr auto crt_ctor = "(Ljava/math/BigInteger;Ljava/math/BigInteger;Ljava/math/BigInteger;"
                                      "Ljava/math/BigInteger;Ljava/math/BigInteger;Ljava/math/BigInteger;"
                                      "Ljava/math/BigInteger;Ljava/math/BigInteger;)V";
            auto crt_key = f.Generate(factory, f.Construct(kCrtSpec, crt_ctor, crt_fields), true);
            CHECK(f.Class(crt_key) == kRsaPrivate);
            auto crt_spec = f.Spec(factory, crt_key, kCrtSpec);
            for (const auto& [member, value] : kCrtComponents) {
                CheckComponent(f, crt_key, member, value);
                CheckComponent(f, crt_spec, member, value);
            }
            CheckEqual(f, crt_key, f.Generate(factory, crt_spec, true));
            CheckEncoding(f, crt_key, kPrivate);

            auto priv_spec = f.Spec(factory, private_key, kPrivateSpec);
            auto exponent_only = f.Generate(factory, priv_spec, true);
            CheckComponent(f, exponent_only, kN, kModulus);
            CheckComponent(f, exponent_only, kD, kPrivateExponent);
            auto exponent_spec = f.Spec(factory, exponent_only, kPrivateSpec);
            CheckComponent(f, exponent_spec, kN, kModulus);
            CheckComponent(f, exponent_spec, kD, kPrivateExponent);
            CheckEqual(f, exponent_only, f.Generate(factory, exponent_spec, true));
            for (auto key : {public_key, private_key, exponent_only})
                CHECK(f.Call(factory, "translateKey", "(Ljava/security/Key;)Ljava/security/Key;", {key}).ref ==
                      key.ref);

            auto cipher = f.Cipher("RSA/ECB/PKCS1Padding");
            f.CipherInit(cipher, 2, exponent_only);
            CHECK(f.Read(f.Final(cipher, f.Bytes(kPkcs1))) == Hex(kMessage));
            auto signature = f.Signature("SHA256WithRSA");
            CHECK(f.Read(f.Sign(signature, exponent_only, f.Bytes(kMessage))) == Hex(kSignatureSHA256));

            for (auto data : {kEcPublic, std::string_view("3003020100")}) {
                auto spec = f.Construct(kX509Spec, "([B)V", {f.Bytes(data)});
                f.Exception(f.TryVirtual(factory, "generatePublic",
                                         "(Ljava/security/spec/KeySpec;)Ljava/security/PublicKey;", {spec}),
                            "Ljava/security/spec/InvalidKeySpecException;");
            }
            for (auto data : {kEcPrivate, std::string_view("3003020100")}) {
                auto spec = f.Construct(kPkcs8Spec, "([B)V", {f.Bytes(data)});
                f.Exception(f.TryVirtual(factory, "generatePrivate",
                                         "(Ljava/security/spec/KeySpec;)Ljava/security/PrivateKey;", {spec}),
                            "Ljava/security/spec/InvalidKeySpecException;");
            }
            auto large = f.Construct(kX509Spec, "([B)V", {f.Bytes(std::vector<std::byte>(1024 * 1024 + 1))});
            f.Exception(f.TryVirtual(factory, "generatePublic",
                                     "(Ljava/security/spec/KeySpec;)Ljava/security/PublicKey;", {large}),
                        "Ljava/security/spec/InvalidKeySpecException;");
            auto negative = f.Construct("Ljava/math/BigInteger;", "([B)V", {f.Bytes("ff")});
            auto bad_spec = f.Construct(kPublicSpec, "(Ljava/math/BigInteger;Ljava/math/BigInteger;)V",
                                        {negative, f.BigInt(kEValue)});
            f.Exception(f.TryVirtual(factory, "generatePublic",
                                     "(Ljava/security/spec/KeySpec;)Ljava/security/PublicKey;", {bad_spec}),
                        "Ljava/security/spec/InvalidKeySpecException;");
        });
    }
}

TEST_CASE("API19 RSA Cipher dispatches exact spellings and handles raw and PKCS1 blocks") {
    for (auto backend : kBackends) {
        CAPTURE(std::string_view(backend == InterpreterBackend::threaded ? "threaded" : "switch"));
        RsaFixture f(backend);
        f.Run([&] {
            auto factory = f.Factory();
            auto public_key = f.PublicKey(factory);
            auto private_key = f.PrivateKey(factory);
            auto message = f.Bytes(kMessage);
            auto raw_message = Hex(kMessage);
            raw_message.insert(raw_message.begin(), 256 - raw_message.size(), std::byte{0});
            for (const auto* mode : {"ECB", "None"}) {
                for (auto raw : {false, true}) {
                    CAPTURE(mode);
                    CAPTURE(raw);
                    auto cipher = f.Cipher(std::string("RSA/") + mode + (raw ? "/NoPadding" : "/PKCS1Padding"));
                    CHECK(f.Provider(cipher) == kConscrypt);
                    auto ciphertext = f.Bytes(raw ? kRaw : kPkcs1);
                    f.CipherInit(cipher, 2, private_key);
                    CHECK(f.Read(f.Final(cipher, ciphertext)) == (raw ? raw_message : Hex(kMessage)));
                    for (bool private_encrypt : {false, true}) {
                        f.CipherInit(cipher, 1, private_encrypt ? private_key : public_key);
                        auto encrypted = f.Final(cipher, message);
                        if (raw && !private_encrypt) CHECK(f.Read(encrypted) == Hex(kRaw));
                        f.CipherInit(cipher, 2, private_encrypt ? public_key : private_key);
                        CHECK(f.Read(f.Final(cipher, encrypted)) == (raw ? raw_message : Hex(kMessage)));
                    }
                }
            }

            auto bare = f.Cipher("RSA");
            CHECK(f.Provider(bare) == "OGPlayCrypto");
            f.CipherInit(bare, 1, public_key);
            CHECK(f.Call(bare, "getBlockSize", "()I").AsInt() == 255);
            CHECK(f.Read(f.Final(bare, message)) == Hex(kRaw));
            f.CipherInit(bare, 2, private_key);
            CHECK(f.Read(f.Final(bare, f.Bytes(kRaw))) == Hex(kMessage));

            auto pkcs = f.Cipher("RSA/ECB/PKCS1Padding");
            f.CipherInit(pkcs, 1, public_key);
            f.Exception(f.TryVirtual(pkcs, "doFinal", "([B)[B", {f.Bytes(std::vector<std::byte>(257))}),
                        "Ljavax/crypto/IllegalBlockSizeException;");
            pkcs = f.Cipher("RSA/ECB/PKCS1Padding");
            f.CipherInit(pkcs, 2, private_key);
            f.Exception(f.TryVirtual(pkcs, "doFinal", "([B)[B", {f.Bytes(kRaw)}), "Ljavax/crypto/BadPaddingException;");

            auto aes = f.Construct("Ljavax/crypto/spec/SecretKeySpec;", "([BLjava/lang/String;)V",
                                   {f.Bytes("00112233445566778899aabbccddeeff"), f.String("AES")});
            pkcs = f.Cipher("RSA/ECB/PKCS1Padding");
            f.CipherInit(pkcs, 3, public_key);
            auto wrapped = f.Call(pkcs, "wrap", "(Ljava/security/Key;)[B", {aes});
            f.CipherInit(pkcs, 4, private_key);
            auto unwrapped = f.Call(pkcs, "unwrap", "([BLjava/lang/String;I)Ljava/security/Key;",
                                    {wrapped, f.String("AES"), VmValue::Int(3)});
            CheckEncoding(f, unwrapped, "00112233445566778899aabbccddeeff");
        });
    }
}

TEST_CASE("API19 RSA OAEP matches OpenSSL vectors and supports independent MGF1 and label") {
    constexpr auto spec_type = "Ljavax/crypto/spec/OAEPParameterSpec;";
    constexpr auto source_type = "Ljavax/crypto/spec/PSource$PSpecified;";
    constexpr auto spec_ctor =
        "(Ljava/lang/String;Ljava/lang/String;Ljava/security/spec/AlgorithmParameterSpec;Ljavax/crypto/spec/PSource;)V";
    constexpr auto spec_init = "(ILjava/security/Key;Ljava/security/spec/AlgorithmParameterSpec;)V";
    constexpr auto params_init = "(ILjava/security/Key;Ljava/security/AlgorithmParameters;)V";
    constexpr std::pair<std::string_view, std::string_view> vectors[] = {
        {"SHA-1", kOaepSHA1},     {"SHA-224", kOaepSHA224}, {"SHA-256", kOaepSHA256},
        {"SHA-384", kOaepSHA384}, {"SHA-512", kOaepSHA512},
    };
    for (auto backend : kBackends) {
        CAPTURE(std::string_view(backend == InterpreterBackend::threaded ? "threaded" : "switch"));
        RsaFixture f(backend);
        f.Run([&] {
            auto factory = f.Factory();
            auto pub = f.PublicKey(factory);
            auto priv = f.PrivateKey(factory);
            auto message = f.Bytes(kMessage);
            for (const auto& [hash, ciphertext] : vectors) {
                CAPTURE(hash);
                auto cipher = f.Cipher(std::string("RSA/ECB/OAEPWith") + std::string(hash) + "AndMGF1Padding");
                CHECK(f.Provider(cipher) == "OGPlayCrypto");
                f.CipherInit(cipher, 2, priv);
                CHECK(f.Read(f.Final(cipher, f.Bytes(ciphertext))) == Hex(kMessage));
                f.CipherInit(cipher, 1, pub);
                auto message_size = static_cast<int>(Hex(kMessage).size());
                f.Call(cipher, "update", "([BII)[B", {message, VmValue::Int(0), VmValue::Int(1)});
                auto encrypted =
                    f.Call(cipher, "doFinal", "([BII)[B", {message, VmValue::Int(1), VmValue::Int(message_size - 1)});
                f.CipherInit(cipher, 2, priv);
                CHECK(f.Read(f.Final(cipher, encrypted)) == Hex(kMessage));
            }
            auto cipher = f.Cipher("RSA/None/OAEPPadding");
            f.CipherInit(cipher, 2, priv);
            CHECK(f.Read(f.Final(cipher, f.Bytes(kOaepSHA1))) == Hex(kMessage));
            CHECK(f.Read(f.Final(cipher, f.Bytes(kOaepLeadingZero))) == Hex(kMessage));
            auto mgf =
                f.Construct("Ljava/security/spec/MGF1ParameterSpec;", "(Ljava/lang/String;)V", {f.String("SHA-1")});
            auto label = f.Construct(source_type, "([B)V", {f.Bytes("6c6162656c")});
            auto spec =
                f.Construct(spec_type, spec_ctor, {f.String("SHA-256"), f.String("1.2.840.113549.1.1.8"), mgf, label});
            f.Call(cipher, "init", spec_init, {VmValue::Int(2), priv, spec});
            CHECK(f.Read(f.Final(cipher, f.Bytes(kOaepLabel))) == Hex(kMessage));
            auto params = f.Call(cipher, "getParameters", "()Ljava/security/AlgorithmParameters;");
            auto der = f.Call(params, "getEncoded", "()[B");
            auto restored = f.Static(kParameters, "getInstance",
                                     "(Ljava/lang/String;)Ljava/security/AlgorithmParameters;", {f.String("OAEP")});
            f.Call(restored, "init", "([B)V", {der});
            f.Call(cipher, "init", params_init, {VmValue::Int(2), priv, restored});
            CHECK(f.Read(f.Final(cipher, f.Bytes(kOaepLabel))) == Hex(kMessage));
            auto unknown_mgf =
                f.Construct(spec_type, spec_ctor, {f.String("SHA-256"), f.String("unknown"), mgf, label});
            f.Exception(f.TryVirtual(cipher, "init", spec_init, {VmValue::Int(2), priv, unknown_mgf}),
                        "Ljava/security/InvalidAlgorithmParameterException;");
            f.CipherInit(cipher, 2, priv);
            for (auto malformed : {kOaepLabel, kRaw, std::string_view("01")}) {
                auto error = f.TryVirtual(cipher, "doFinal", "([B)[B", {f.Bytes(malformed)});
                f.Exception(error, "Ljavax/crypto/BadPaddingException;");
                CHECK(error.exception_message == "OAEP decoding error");
            }
            CHECK(f.Read(f.Final(cipher, f.Bytes(kOaepSHA1))) == Hex(kMessage));
        });
    }
}

TEST_CASE("API19 RSA Signature registers aliases and matches deterministic PKCS1 vectors") {
    const std::pair<const char*, std::string_view> vectors[] = {
        {"MD5WithRSA", kSignatureMD5},       {"SHA1WithRSA", kSignatureSHA1},     {"SHA256WithRSA", kSignatureSHA256},
        {"SHA384WithRSA", kSignatureSHA384}, {"SHA512WithRSA", kSignatureSHA512}, {"NONEwithRSA", kRawSignature},
    };
    const std::pair<const char*, const char*> aliases[] = {
        {"MD5WithRSAEncryption", "MD5WithRSA"},
        {"MD5/RSA", "MD5WithRSA"},
        {"1.2.840.113549.1.1.4", "MD5WithRSA"},
        {"1.2.840.113549.2.5with1.2.840.113549.1.1.1", "MD5WithRSA"},
        {"SHA1WithRSAEncryption", "SHA1WithRSA"},
        {"SHA1/RSA", "SHA1WithRSA"},
        {"SHA-1/RSA", "SHA1WithRSA"},
        {"1.2.840.113549.1.1.5", "SHA1WithRSA"},
        {"1.3.14.3.2.29", "SHA1WithRSA"},
        {"1.3.14.3.2.26with1.2.840.113549.1.1.1", "SHA1WithRSA"},
        {"1.3.14.3.2.26with1.2.840.113549.1.1.5", "SHA1WithRSA"},
        {"SHA256WithRSAEncryption", "SHA256WithRSA"},
        {"1.2.840.113549.1.1.11", "SHA256WithRSA"},
        {"2.16.840.1.101.3.4.2.1with1.2.840.113549.1.1.1", "SHA256WithRSA"},
        {"2.16.840.1.101.3.4.2.1with1.2.840.113549.1.1.11", "SHA256WithRSA"},
        {"SHA384WithRSAEncryption", "SHA384WithRSA"},
        {"1.2.840.113549.1.1.12", "SHA384WithRSA"},
        {"2.16.840.1.101.3.4.2.2with1.2.840.113549.1.1.1", "SHA384WithRSA"},
        {"SHA512WithRSAEncryption", "SHA512WithRSA"},
        {"1.2.840.113549.1.1.13", "SHA512WithRSA"},
        {"2.16.840.1.101.3.4.2.3with1.2.840.113549.1.1.1", "SHA512WithRSA"},
    };
    for (auto backend : kBackends) {
        CAPTURE(std::string_view(backend == InterpreterBackend::threaded ? "threaded" : "switch"));
        RsaFixture f(backend);
        f.Run([&] {
            auto factory = f.Factory();
            auto pub = f.PublicKey(factory);
            auto priv = f.PrivateKey(factory);
            auto message = f.Bytes(kMessage);
            for (const auto& [algorithm, vector] : vectors) {
                CAPTURE(algorithm);
                auto signature = f.Signature(algorithm);
                CHECK(f.Provider(signature) == kConscrypt);
                auto signed_data = f.Sign(signature, priv, message);
                CHECK(f.Read(signed_data) == Hex(vector));
                CHECK(f.Verify(signature, pub, message, signed_data) == 1);
                auto tampered = Hex(vector);
                tampered.back() ^= std::byte{1};
                CHECK(f.Verify(signature, pub, message, f.Bytes(tampered)) == 0);
                f.Call(signature, "initSign", "(Ljava/security/PrivateKey;)V", {priv});
                f.Call(signature, "update", "(B)V", {VmValue::Int('x')});
                CHECK(f.Read(f.Sign(signature, priv, message)) == Hex(vector));
            }
            auto provider = f.Call(f.Signature("SHA256WithRSA"), "getProvider", "()Ljava/security/Provider;");
            for (const auto& [alias, canonical] : aliases) {
                CAPTURE(alias);
                CHECK(f.Text(f.Call(provider, "getProperty", "(Ljava/lang/String;)Ljava/lang/String;",
                                    {f.String(std::string("Alg.Alias.Signature.") + alias)})) == canonical);
                CHECK(f.Provider(f.Signature(alias)) == kConscrypt);
            }
            for (auto name : {"RSASSA-PSS", "MD2withRSA", "SHA256WithRSA/PSS"}) {
                f.Exception(f.TryStatic(kSignature, "getInstance", "(Ljava/lang/String;)Ljava/security/Signature;",
                                        {f.String(name)}),
                            "Ljava/security/NoSuchAlgorithmException;");
            }
        });
    }
}

TEST_CASE("API19 RSA KeyPairGenerator supports RSAKeyGenParameterSpec and the 2048-bit default") {
    for (auto backend : kBackends) {
        CAPTURE(std::string_view(backend == InterpreterBackend::threaded ? "threaded" : "switch"));
        RsaFixture f(backend);
        f.Run([&] {
            auto generator = f.Static(kGenerator, "getInstance", "(Ljava/lang/String;)Ljava/security/KeyPairGenerator;",
                                      {f.String("1.2.840.113549.1.1.1")});
            CHECK(f.Provider(generator) == kConscrypt);
            const auto start = hal::Clock::SteadyTimestampNs();
            auto pair = f.Call(generator, "generateKeyPair", "()Ljava/security/KeyPair;");
            MESSAGE(std::string_view(backend == InterpreterBackend::threaded ? "threaded" : "switch")
                    << " RSA-2048 generation " << (hal::Clock::SteadyTimestampNs() - start) / 1000000 << " ms");
            auto pub = f.Call(pair, "getPublic", "()Ljava/security/PublicKey;");
            CHECK(f.Call(f.Call(pub, kN, kBigInt), "bitLength", "()I").AsInt() == 2048);
            CheckComponent(f, pub, kE, kEValue);
            auto spec = f.Construct("Ljava/security/spec/RSAKeyGenParameterSpec;", "(ILjava/math/BigInteger;)V",
                                    {VmValue::Int(1024), f.BigInt("03")});
            f.Call(generator, "initialize", "(Ljava/security/spec/AlgorithmParameterSpec;)V", {spec});
            pair = f.Call(generator, "generateKeyPair", "()Ljava/security/KeyPair;");
            pub = f.Call(pair, "getPublic", "()Ljava/security/PublicKey;");
            auto priv = f.Call(pair, "getPrivate", "()Ljava/security/PrivateKey;");
            CHECK(f.Call(f.Call(pub, kN, kBigInt), "bitLength", "()I").AsInt() == 1024);
            CheckComponent(f, pub, kE, "03");
            auto message = f.Bytes(kMessage);
            auto sig = f.Signature("SHA256WithRSA");
            CHECK(f.Verify(sig, pub, message, f.Sign(sig, priv, message)) == 1);
            auto cipher = f.Cipher("RSA/ECB/PKCS1Padding");
            f.CipherInit(cipher, 1, pub);
            auto encrypted = f.Final(cipher, message);
            f.CipherInit(cipher, 2, priv);
            CHECK(f.Read(f.Final(cipher, encrypted)) == Hex(kMessage));
        });
    }
}

TEST_CASE("API19 RSA private keys restored from BKS sign and decrypt with their certificate") {
    for (auto backend : kBackends) {
        CAPTURE(std::string_view(backend == InterpreterBackend::threaded ? "threaded" : "switch"));
        RsaFixture f(backend);
        f.Run([&] {
            auto store = f.Static("Ljava/security/KeyStore;", "getInstance",
                                  "(Ljava/lang/String;)Ljava/security/KeyStore;", {f.String("BKS")});
            auto null = VmValue::Ref(VmObjectRef{});
            auto password = f.Chars("keypass");
            f.Call(store, "load", "(Ljava/io/InputStream;[C)V", {null, null});
            auto factory = f.Factory();
            auto key = f.Generate(
                factory, f.Construct(kPkcs8Spec, "([B)V", {f.Bytes(test::ReadTlsFixture("client.pkcs8"))}), true);
            auto cert_factory =
                f.Static("Ljava/security/cert/CertificateFactory;", "getInstance",
                         "(Ljava/lang/String;)Ljava/security/cert/CertificateFactory;", {f.String("X.509")});
            auto input =
                f.Construct("Ljava/io/ByteArrayInputStream;", "([B)V", {f.Bytes(test::ReadTlsFixture("client.der"))});
            auto cert = f.Call(cert_factory, "generateCertificate",
                               "(Ljava/io/InputStream;)Ljava/security/cert/Certificate;", {input});
            constexpr auto cert_type = "Ljava/security/cert/Certificate;";
            auto chain = f.vm.Model().NewObjectArray(f.linker.ResolveDescriptor("[Ljava/security/cert/Certificate;"),
                                                     f.linker.ResolveDescriptor(cert_type), 1);
            VmCallOutcome array_result;
            array_result.value = VmValue::Ref(chain);
            (void)f.Value(array_result);
            f.vm.Model().SetObjectElement(chain, 0, cert.ref);
            auto alias = f.String("rsa");
            f.Call(store, "setKeyEntry", "(Ljava/lang/String;Ljava/security/Key;[C[Ljava/security/cert/Certificate;)V",
                   {alias, key, password, VmValue::Ref(chain)});
            cert = f.Call(store, "getCertificate", "(Ljava/lang/String;)Ljava/security/cert/Certificate;", {alias});
            auto pub = f.Call(cert, "getPublicKey", "()Ljava/security/PublicKey;");
            auto priv = f.Call(store, "getKey", "(Ljava/lang/String;[C)Ljava/security/Key;", {alias, password});
            CHECK(f.Class(pub) == kRsaPublic);
            CHECK(f.Class(priv) == kRsaPrivate);
            CHECK(f.Read(f.Call(priv, "getEncoded", "()[B")) == test::ReadTlsFixture("client.pkcs8"));
            auto message = f.Bytes(kMessage);
            auto sig = f.Signature("SHA256WithRSA");
            CHECK(f.Verify(sig, pub, message, f.Sign(sig, priv, message)) == 1);
            auto cipher = f.Cipher("RSA/ECB/PKCS1Padding");
            f.CipherInit(cipher, 1, pub);
            auto encrypted = f.Final(cipher, message);
            f.CipherInit(cipher, 2, priv);
            CHECK(f.Read(f.Final(cipher, encrypted)) == Hex(kMessage));
        });
    }
}

TEST_CASE("API19 RSA key and signature tokens are freed by GC and teardown") {
    for (auto backend : kBackends) {
        CAPTURE(std::string_view(backend == InterpreterBackend::threaded ? "threaded" : "switch"));
        RsaFixture f(backend);
        auto field = [&](VmValue owner, const char* name) {
            auto id = f.linker.FindFieldRecursive(f.vm.Model().ObjectClass(owner.ref), name, "J");
            REQUIRE(id);
            auto slot = f.linker.Field(*id).slot;
            auto slots = f.vm.Model().InstanceSlots(owner.ref);
            auto bits = (static_cast<std::uint64_t>(slots[slot + 1].bits) << 32U) | slots[slot].bits;
            return VmValue::Long(static_cast<std::int64_t>(bits));
        };
        auto create = [&] {
            auto key = f.PrivateKey(f.Factory());
            auto owner = f.Call(key, "getOpenSSLKey", "()Lcom/android/org/conscrypt/OpenSSLKey;");
            auto sig = f.Signature("SHA256WithRSA");
            f.Call(sig, "initSign", "(Ljava/security/PrivateKey;)V", {key});
            f.Call(sig, "update", "(B)V", {VmValue::Int('x')});
            return std::pair{field(owner, "ctx"), field(sig, "ctx")};
        };
        std::size_t baseline = 0;
        std::vector<std::pair<VmValue, VmValue>> tokens;
        f.Run([&] {
            baseline = f.vm.GuestNativeResourceCount();
            for (int i = 0; i < 24; ++i)
                tokens.push_back(create());
            CHECK(f.vm.GuestNativeResourceCount() >= baseline + 48);
        });
        // A Signature reaches its OpenSSLKey until its own finalizer completes.
        // Let the finalizer thread run between collections of those two owners.
        auto remaining = f.vm.GuestNativeResourceCount();
        const auto deadline = hal::Clock::SteadyTimestampNs() + UINT64_C(5000000000);
        while (remaining > baseline && hal::Clock::SteadyTimestampNs() < deadline) {
            f.Run([&] { static_cast<void>(f.vm.CollectGarbage("RSA owners dropped")); });
            std::this_thread::yield();
            remaining = f.vm.GuestNativeResourceCount();
        }
        CHECK(remaining <= baseline);
        f.Run([&] {
            auto check_freed = [&](const std::pair<VmValue, VmValue>& pair) {
                const auto& [key, ctx] = pair;
                f.Exception(f.TryStatic(kNative, "EVP_PKEY_size", "(J)I", {key}), "Ljava/lang/IllegalStateException;");
                f.Exception(f.TryStatic(kNative, "EVP_SignUpdate", "(J[BII)V",
                                        {ctx, f.Bytes("00"), VmValue::Int(0), VmValue::Int(1)}),
                            "Ljava/lang/IllegalStateException;");
            };
            for (const auto& pair : tokens)
                check_freed(pair);
            auto live = create();
            CHECK(f.Static(kNative, "EVP_PKEY_size", "(J)I", {live.first}).AsInt() == 256);
            f.vm.ReleaseGuestNativeResources(true);
            CHECK(f.vm.GuestNativeResourceCount() == 0);
            check_freed(live);
        });
    }
}
