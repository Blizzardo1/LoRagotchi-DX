#include <gtest/gtest.h>
#include "../../examples/companion_radio/ui-new/LoRaFishText.h"

TEST(LoRaFishText, RecognizesEverySupportedFishAndPresentationSelector) {
  for (const char* name : {"🐟", "🐠", "🐡", "🦈", "Tank🐟", "🐠 Tank",
                           "Tank 🐡\xEF\xB8\x8F", "Tank 🦈\xEF\xB8\x8E"})
    EXPECT_TRUE(LoRaFishText::hasFish(name, strlen(name)));
}
TEST(LoRaFishText, MarkerIsOptInNotOtherMarineOrFishRelatedObjects) {
  for (const char* name : {"fish", "><>", "🐬", "🐳", "🐋", "🐙", "🪼", "🎣", "🍥", "🎏", "", "Repeater"})
    EXPECT_FALSE(LoRaFishText::hasFish(name, strlen(name)));
  EXPECT_FALSE(LoRaFishText::hasFish(nullptr, 32));
}
TEST(LoRaFishText, ScansBoundedNonterminatedNamesAndDoesNotReadBeyondThem) {
  const char name[]={'T',char(0xF0),char(0x9F),char(0x90),char(0x9F)};
  EXPECT_TRUE(LoRaFishText::hasFish(name, sizeof(name)));
  for (size_t n=0;n<sizeof(name);++n) EXPECT_FALSE(LoRaFishText::hasFish(name,n));
}
TEST(LoRaFishText, RejectsMalformedOverlongSurrogateAndOutOfRangeSequences) {
  for(const char* invalid : {"\xF0\x80\x90\x9F", "\xED\xA0\x80", "\xF4\x90\x80\x80",
                             "\xC0\xAF", "\xFF\x90\x9F", "\xF0\x9F", "\x80"})
    EXPECT_FALSE(LoRaFishText::hasFish(invalid,strlen(invalid)));
}
TEST(LoRaFishText, FishBecomePlainFontSymbolsWithoutMutatingTheSource) {
  char raw[]="A 🐟🐠🐡🦈 Z";
  char out[32];LoRaFishText::display(out,sizeof(out),raw,sizeof(raw));
  EXPECT_STREQ(out,"A ><>><>><>><> Z");
  EXPECT_STREQ(raw,"A 🐟🐠🐡🦈 Z");
}
TEST(LoRaFishText, UnknownUnicodeBecomesSpacesNotRawUtf8Bytes) {
  char out[64];
  LoRaFishText::display(out,sizeof(out),"Hi 🔋🙂é\n!",64);
  EXPECT_STREQ(out,"Hi     !");
  LoRaFishText::display(out,sizeof(out),"🐟️ 🧑🏽‍🔧 🇺🇸",64);
  for (char c : out) { if(!c)break; EXPECT_GE(c,32);EXPECT_LE(c,126); }
  EXPECT_STREQ(out,"><>      ");
}
TEST(LoRaFishText, TinyOutputsRemainTerminatedWithoutPartialFishTokens) {
  for(size_t size=1;size<=4;++size) {
    char out[8];memset(out,'!',sizeof(out));
    LoRaFishText::display(out,size,"🐟",4);
    EXPECT_STREQ(out,size==4?"><>":"");
    EXPECT_EQ(out[size],'!');
  }
  char sentinel='!';LoRaFishText::display(&sentinel,0,"fish",4);EXPECT_EQ(sentinel,'!');
  LoRaFishText::display(nullptr,4,"fish",4);
}
TEST(LoRaFishText, TruncatesRenderedColumnsNotUtf8Bytes) {
  char out[16];LoRaFishText::display(out,sizeof(out),"🐟ABCDEFGHIJKLMNO",32);
  EXPECT_STREQ(out,"><>ABCDEFGHIJKL");
  LoRaFishText::display(out,sizeof(out),"ABCDEFGHIJKLM🐟",32);
  EXPECT_STREQ(out,"ABCDEFGHIJKLM");
}
TEST(LoRaFishText, RawProtocolPrefixPreservesUtf8AndNeverCutsAnEmoji) {
  char out[16];LoRaFishText::rawPrefix(out,sizeof(out),"Tank🐠",32);
  EXPECT_STREQ(out,"Tank🐠");
  LoRaFishText::rawPrefix(out,sizeof(out),"ABCDEFGHIJKL🐟",32);
  EXPECT_STREQ(out,"ABCDEFGHIJKL");
  LoRaFishText::rawPrefix(out,sizeof(out),"ABCDEFGHIJK🐟",32);
  EXPECT_STREQ(out,"ABCDEFGHIJK🐟");
}
TEST(LoRaFishText, RawPrefixHandlesMissingAndInvalidInputSafely) {
  char out[8];LoRaFishText::rawPrefix(out,sizeof(out),nullptr,32);EXPECT_STREQ(out,"");
  LoRaFishText::rawPrefix(out,sizeof(out),"A\xFF",2);EXPECT_STREQ(out,"A");
  char sentinel='!';LoRaFishText::rawPrefix(&sentinel,0,"fish",4);EXPECT_EQ(sentinel,'!');
}
int main(int argc,char** argv) {
  ::testing::InitGoogleTest(&argc,argv);
  return RUN_ALL_TESTS();
}
