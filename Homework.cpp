#include "image.h"
#include "tiff_reader.h"
#include "bmp_writer.h"
#include "deskew.h"
#include "test.h"

#include <cstdio>
#include <cstring>

int main(int argc, char* argv[]) {

    const char* inputFile = (argc > 1) ? argv[1] : "ex.tif";

    Image src;
    if (!loadTIFF(inputFile, src)) {
        return 1;
    }

    uint8_t thresh = otsuThreshold(src);
    saveBinaryBMP("binary.bmp", src, thresh);

    Image textOnly = removeNonTextObjects(src, thresh);
    saveBinaryBMP("text_only.bmp", textOnly, thresh);

    double skewAngle = detectSkewAngle(textOnly, thresh);
    double correction = -skewAngle;

    printf("\nSkew    : %.4f deg\n", skewAngle);
    printf("Correct : %.4f deg\n\n", correction);

    Image rotText = rotateImage(textOnly, correction);
    saveBinaryBMP("rotated_text.bmp", rotText, thresh);

    Image result = rotateImage(src, correction);
    saveBMP("result.bmp", result);

    runAllTests();
    return 0;
}
