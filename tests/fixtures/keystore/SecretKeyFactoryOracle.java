// Isolated BC 1.50 oracle; never compiled into the production BootDex.
// javac -cp bcprov-jdk15on-1.50.jar SecretKeyFactoryOracle.java
// java -cp ".;bcprov-jdk15on-1.50.jar" SecretKeyFactoryOracle
import javax.crypto.SecretKeyFactory;
import javax.crypto.spec.PBEKeySpec;
import org.bouncycastle.jce.provider.BouncyCastleProvider;

public final class SecretKeyFactoryOracle {
    public static void main(String[] args) throws Exception {
        SecretKeyFactory factory = SecretKeyFactory.getInstance(
                "PBEWITHSHAAND256BITAES-CBC-BC", new BouncyCastleProvider());
        char[][] passwords = {"password".toCharArray(), new char[0],
                {'\u5bc6', '\u7801', '\0', '\ud83d', '\ude00'}};
        for (char[] password : passwords) {
            for (int iterations : new int[] {1, 1024}) {
                byte[] key = factory.generateSecret(new PBEKeySpec(password,
                        new byte[] {1,2,3,4,5,6,7,8}, iterations, 128)).getEncoded();
                StringBuilder hex = new StringBuilder();
                for (byte b : key) hex.append(String.format("%02x", b & 255));
                System.out.println(hex);
            }
        }
    }
}
