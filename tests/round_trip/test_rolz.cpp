// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <string>

#include <gtest/gtest.h>

#include "openzl/codecs/rolz/encode_rolz_kernel.h"
#include "openzl/decompress/internal.h"

#include "rolz_test_data.h"

namespace {
std::string const kLoremIpsum = R"(
Lorem ipsum dolor sit amet, consectetur adipiscing elit. In molestie mattis purus, et blandit arcu luctus vitae. In ut neque nisl. Ut et augue mattis, euismod dui ut, rutrum sem. Pellentesque semper a nibh eu laoreet. Sed ac vestibulum mauris, id tempus felis. Fusce id ex quis lectus ultrices lacinia. Nulla tortor felis, aliquet vitae ligula sit amet, ornare tincidunt felis. Integer consectetur sagittis justo id convallis. Aenean suscipit maximus nisi, a pellentesque libero tincidunt sed. Curabitur pharetra sem risus, ut aliquam nulla pretium vel. Nam orci tellus, fringilla et nisl in, rutrum maximus odio. Integer nulla sapien, finibus at nibh nec, dignissim euismod diam. Duis at mauris ipsum. Ut volutpat bibendum viverra. Nam nec fermentum ipsum.

Donec varius felis ut neque suscipit pellentesque. Praesent placerat vel arcu quis vulputate. Vestibulum fermentum mauris sed metus rhoncus, sit amet rutrum dui aliquam. Nulla ut neque lobortis, euismod ligula id, blandit quam. Integer sit amet consequat dui. Vestibulum vestibulum libero libero, ac iaculis ligula facilisis id. Curabitur vitae leo tellus. Cras laoreet tincidunt nibh sit amet porta. In quis lobortis turpis, a eleifend risus. Vestibulum ultricies elit et scelerisque aliquet. Sed ac lorem in dolor suscipit hendrerit. Morbi bibendum risus odio, et volutpat nisl egestas in. Phasellus accumsan ut velit sit amet molestie.

Nulla est mauris, dignissim semper facilisis a, interdum nec mauris. Pellentesque habitant morbi tristique senectus et netus et malesuada fames ac turpis egestas. Mauris at convallis leo. Vivamus nulla arcu, ornare sed eros eget, elementum tempor dui. Nam feugiat dignissim ligula, non vestibulum nisi lacinia eu. Integer fermentum arcu purus, ut hendrerit orci interdum sed. Etiam sagittis nisl nisi, a volutpat lectus feugiat eu. Quisque pellentesque posuere justo, fermentum ornare lacus finibus sed. Suspendisse ullamcorper venenatis viverra. Pellentesque habitant morbi tristique senectus et netus et malesuada fames ac turpis egestas. In hac habitasse platea dictumst. Ut eu lacus arcu. Duis finibus venenatis libero ut fermentum. Duis luctus consequat congue. Quisque id nibh id enim bibendum ultricies ut fringilla est.

Maecenas id dui sed quam lobortis consequat. Fusce faucibus risus at pellentesque laoreet. Nullam et eleifend purus. Cras laoreet suscipit egestas. Orci varius natoque penatibus et magnis dis parturient montes, nascetur ridiculus mus. Maecenas tristique faucibus ex, eleifend volutpat nisl porta a. Donec pulvinar lacus orci, dignissim posuere velit congue ac. Maecenas arcu turpis, aliquam et dui et, sagittis ultricies nisl. Duis lacus urna, auctor vitae urna vel, maximus consequat augue. Phasellus vitae maximus sapien. Etiam lacinia turpis ut ipsum dignissim consectetur. Etiam a magna ipsum. Maecenas quam lectus, gravida nec tortor nec, gravida dapibus velit. Quisque dapibus nisi quis sodales fermentum. Nulla pulvinar malesuada est. Proin faucibus vitae orci in ullamcorper.

Mauris felis risus, tincidunt eget sagittis quis, rhoncus at felis. Aliquam ullamcorper eleifend tortor eget tristique. Vivamus mattis leo in lorem tincidunt ornare. Nam et neque massa. Proin tellus ipsum, eleifend vitae mauris imperdiet, dictum pretium mi. Nullam nec massa vel augue mollis faucibus. Mauris suscipit fringilla diam, quis dapibus quam sodales et. Donec eu lorem lacus. Suspendisse at urna et diam tincidunt bibendum. Sed tincidunt vel ligula vel dapibus. Sed condimentum molestie augue a pulvinar. Duis iaculis enim id mi faucibus fringilla. Quisque ex lectus, consequat ac tellus sit amet, lacinia euismod ligula. Aenean sit amet aliquet neque. Duis tortor metus, feugiat quis lacus dapibus, rhoncus semper metus.

Nulla quis ornare nisl, at tempus lectus. Vestibulum in luctus odio. Donec blandit libero nec ex vehicula, quis aliquam eros accumsan. Mauris rhoncus dolor nec sapien efficitur vestibulum. Vestibulum eu ligula lacus. Aliquam erat volutpat. Integer molestie massa orci, vel sollicitudin leo commodo ac. Phasellus laoreet auctor mauris. Maecenas vitae tincidunt nulla. Morbi condimentum elementum ullamcorper. Nam quis turpis lorem. Ut finibus eleifend convallis. Nam feugiat a odio at auctor. Aenean tempor maximus elit. Sed eget nibh a mauris ornare finibus.

Etiam at enim eu mi placerat ultrices id nec nulla. Praesent justo nisl, tincidunt sit amet leo et, auctor condimentum dolor. Vestibulum id libero elit. Vivamus hendrerit eu sapien dignissim hendrerit. Aliquam ullamcorper metus ut nunc varius, a fringilla urna aliquam. Praesent sit amet nisl at quam lacinia bibendum sed a tellus. Curabitur condimentum nulla at lectus eleifend hendrerit. Fusce neque nunc, fringilla nec lorem non, mollis tristique augue. Sed eu porttitor arcu. Ut ut convallis nibh. Donec eget scelerisque dui. Morbi ultrices ligula nec dui faucibus, id lacinia eros porttitor. Phasellus iaculis ultricies mauris nec consectetur. Curabitur sed lacus in odio maximus mollis. In tristique consectetur odio, eget facilisis tortor placerat accumsan.

Nunc at bibendum arcu, non accumsan elit. Vestibulum ante ipsum primis in faucibus orci luctus et ultrices posuere cubilia Curae; Duis viverra, lorem quis mattis accumsan, tellus sapien varius diam, ac cursus magna tortor ut ex. Morbi finibus ante vel purus facilisis cursus. Nulla facilisi. Integer gravida faucibus lorem nec efficitur. Sed quam purus, rhoncus iaculis nulla id, tincidunt bibendum ipsum. Nullam gravida, nisl quis accumsan rhoncus, tellus diam bibendum sapien, sed iaculis massa ante eu magna. Nunc arcu massa, pulvinar a lorem ac, convallis lobortis risus. Nunc ullamcorper, felis non lacinia aliquam, felis lectus scelerisque tortor, et fermentum metus orci id velit. Maecenas nec ante eget diam faucibus blandit varius non urna. Class aptent taciti sociosqu ad litora torquent per conubia nostra, per inceptos himenaeos. Nullam erat magna, semper ac hendrerit vel, pulvinar id orci. Nulla et euismod ante.

Nunc sit amet odio volutpat, interdum augue a, efficitur elit. Suspendisse sed malesuada nulla. Aliquam erat augue, molestie non arcu et, congue mollis mi. Proin non magna eu nisi lobortis dictum eget tristique purus. Sed lacus nulla, elementum in ultrices quis, sagittis eu lectus. Etiam at lorem egestas, feugiat ante vel, viverra diam. Pellentesque pharetra est non nunc tristique malesuada.

Donec blandit vehicula purus, quis semper ligula commodo in. Phasellus mattis nunc nec pharetra placerat. Donec a fermentum enim. Nam feugiat metus lacinia metus lobortis, ac lobortis nisi egestas. Cras id libero tortor. Maecenas posuere sem nulla, eu lacinia nulla imperdiet eget. Nam pharetra sed tortor nec ullamcorper. Donec eu imperdiet ante, sit amet tempus ipsum. Quisque eget risus faucibus, tristique eros eu, elementum odio. Nam aliquet iaculis augue ut molestie. Suspendisse aliquam malesuada arcu sit amet semper. Sed ut aliquet turpis. Quisque imperdiet pretium diam vel ullamcorper. Donec in venenatis eros. Aenean consectetur sodales quam sed consequat.

Etiam vestibulum nec nibh sed pulvinar. Etiam non consectetur massa, a placerat augue. Vestibulum ante ipsum primis in faucibus orci luctus et ultrices posuere cubilia Curae; Mauris et enim feugiat, pharetra nulla vehicula, efficitur eros. Lorem ipsum dolor sit amet, consectetur adipiscing elit. Nam rhoncus posuere ipsum, nec convallis diam tempor porta. In vulputate sed purus at fermentum.

Sed consectetur nisl condimentum nulla facilisis mattis. Fusce eu mi ut massa finibus aliquet. Fusce efficitur consequat dui, cursus interdum odio aliquet vel. Maecenas pretium, nulla in convallis mollis, lacus lacus lacinia mauris, at vehicula arcu eros nec mauris. Aliquam metus elit, vehicula vitae lorem et, pellentesque malesuada magna. Aliquam ut purus sem. Proin nec maximus magna. Maecenas eget lectus lobortis, imperdiet justo eget, tincidunt sem. Nullam auctor elementum risus. Nulla fringilla odio sit amet massa placerat bibendum.

Nunc tristique quis libero et ultrices. Donec a gravida enim, non ultrices mi. Aliquam tempor dui et quam vestibulum blandit. Nullam dictum tristique elit, non bibendum nunc sollicitudin eget. Nam venenatis sem mattis tincidunt porttitor. Ut ultricies vestibulum leo ac tincidunt. Sed lacinia lectus id ex rutrum, id accumsan arcu molestie. Proin placerat maximus bibendum. Sed blandit varius mollis.

Phasellus accumsan felis a est vestibulum, non commodo risus scelerisque. Sed feugiat est orci, ac mollis nibh consequat eu. Nam a justo nisl. In at pellentesque tortor. Phasellus dapibus, velit non rhoncus sollicitudin, lectus nibh tempus enim, vel porta nulla nibh ut nisi. Mauris scelerisque auctor sapien a lacinia. Nam consectetur turpis sit amet diam consectetur, in sollicitudin ex ullamcorper.

Praesent vel varius nisi. Curabitur laoreet, leo ac tempor vulputate, elit ipsum efficitur neque, tincidunt placerat neque nunc ac quam. In neque eros, bibendum accumsan mi lobortis, molestie aliquet purus. Mauris nec mollis ligula. Curabitur lacinia urna vel est aliquam, ac luctus lacus finibus. Proin dapibus mauris vitae sem eleifend, vitae rutrum nunc semper. Aenean ac tortor vitae nisi malesuada molestie. Sed auctor, tortor eget gravida maximus, leo velit cursus odio, sed feugiat enim diam nec neque. In scelerisque consectetur lectus ac semper. Curabitur suscipit id odio vitae ultrices. Quisque nec nunc sapien. Proin ornare porta tempor. In a turpis quis orci rutrum ultricies non vel lorem. Nulla eget sollicitudin tellus, commodo egestas diam.

Maecenas iaculis iaculis purus, accumsan malesuada eros suscipit at. Curabitur ligula dolor, varius vel malesuada sed, viverra eget justo. Donec non ipsum sit amet lorem consectetur mattis. Maecenas at tristique ipsum. Vestibulum ante ipsum primis in faucibus orci luctus et ultrices posuere cubilia Curae; Fusce aliquet fringilla dignissim. Curabitur a posuere arcu, sed rutrum eros. Integer massa arcu, condimentum sed porttitor ut, egestas et diam. Cras sed.
)";

void testRoundtrip(std::string const& data)
{
    std::string compressed;
    compressed.resize(ZS_rolzCompressBound(data.size()));
    auto report = ZS_rolzCompress(
            &compressed[0], compressed.size(), data.data(), data.size());
    ASSERT_FALSE(ZL_isError(report));
    std::string decompressed;
    decompressed.resize(data.size());
    report = ZL_rolzDecompress(
            &decompressed[0],
            decompressed.size(),
            compressed.data(),
            ZL_validResult(report));
    ASSERT_FALSE(ZL_isError(report));
    decompressed.resize(ZL_validResult(report));
    ASSERT_EQ(decompressed.size(), data.size());
    ASSERT_EQ(decompressed, data);
}

TEST(RolzTest, loremIpsum)
{
    testRoundtrip(kLoremIpsum);
}

TEST(RolzTest, simple)
{
    testRoundtrip(
            "hello world hello word xxxxxxxxxxxxxx xyxyxyxyxyxyxyxyxyxyxy yello world"
            " abcdefghijklmnopABCDEFGHIJKLMOP hello world hello word xxxxxxxxxx yyyyy"
            "yyyyyyyyyy xyxyxyxyxyxyxyx");
}

TEST(RolzTest, zeros)
{
    testRoundtrip("000000000000000000000000000000000");
}

// This malformed O1 frame exhausts one literal cluster while another cluster
// still has data. The test fails if decompression reads past that cluster.
// Pre-fix, the over-read is only reliably observable under AddressSanitizer.
TEST(RolzTest, craftedFrameFailsCleanly)
{
    std::string decompressed;
    decompressed.resize(8u << 20);
    ZL_Report report = ZL_rolzDecompress(
            &decompressed[0],
            decompressed.size(),
            rolz_test_data::kOobReadFrame,
            sizeof(rolz_test_data::kOobReadFrame));
    ASSERT_TRUE(ZL_isError(report));
}

} // namespace
