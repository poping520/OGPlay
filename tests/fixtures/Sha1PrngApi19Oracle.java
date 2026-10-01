import java.security.*;
public class Sha1PrngApi19Oracle {
 static String hex(byte[] b) { StringBuilder s=new StringBuilder(); for(byte v:b) s.append(String.format("%02x",v & 255)); return s.toString(); }
 static byte[] read(SecureRandom r,int n) { byte[] b=new byte[n];r.nextBytes(b);return b; }
 static SecureRandom fresh(byte[] seed) throws Exception { SecureRandom r=SecureRandom.getInstance("SHA1PRNG",new org.apache.harmony.security.provider.crypto.CryptoProvider());r.setSeed(seed);return r; }
 public static void main(String[] args) throws Exception {
  byte[] seed={0,1,2,3,4,5,6,7}; byte[] extra={(byte)255,0};
  System.out.println("seed8_128 "+hex(read(fresh(seed),128)));
  byte[] seed64=new byte[64];for(int i=0;i<64;++i)seed64[i]=(byte)i;
  System.out.println("seed64_64 "+hex(read(fresh(seed64),64)));
  SecureRandom r=fresh(seed);r.setSeed(extra);System.out.println("append_before_64 "+hex(read(r,64)));
  r=fresh(seed);read(r,21);r.setSeed(extra);System.out.println("reseed_after_43 "+hex(read(r,43)));
  System.out.println("empty_seed_40 "+hex(read(fresh(new byte[0]),40)));
 }
}
