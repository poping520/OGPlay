import java.awt.image.BufferedImage;
import java.io.File;
import javax.imageio.ImageIO;

final class GenerateJpeg {
    public static void main(String[] args) throws Exception {
        BufferedImage image = new BufferedImage(16, 16, BufferedImage.TYPE_INT_RGB);
        for (int y = 0; y < 16; y++) {
            for (int x = 0; x < 16; x++) image.setRGB(x, y, 0x4060a0);
        }
        if (!ImageIO.write(image, "jpeg", new File(args[0]))) {
            throw new IllegalStateException("JDK JPEG writer not available");
        }
    }
}
