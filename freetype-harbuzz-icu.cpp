#include <hb.h>
#include <hb-ft.h>
#include FT_GLYPH_H
#include <unicode/ubidi.h>
#include <unicode/ucnv.h> // For utf8 <-> utf16 conversion
#include <string>
#include <vector>

// Useful links:
// https://www.freetype.org/freetype2/docs/tutorial/step1.html
// https://www.freetype.org/freetype2/docs/tutorial/step2.html
// http://www.manpagez.com/html/harfbuzz/harfbuzz-1.0.4/hello-harfbuzz.php
// http://site.icu-project.org/design/collation/script-reordering

static void
DrawGlyph(unsigned char *image,
          FT_Int width,
          FT_Int height,
          FT_Bitmap *glyphBitmap,
          FT_Int x,
          FT_Int y)
{
  FT_Int xMax = x + glyphBitmap->width;
  FT_Int yMax = y + glyphBitmap->rows;

  for (FT_Int i = x, p = 0; i < xMax; i++, p++)
  {
    for (FT_Int j = y, q = 0; j < yMax; j++, q++)
    {
      if ( i < 0 || j < 0 || i >= width || j >= height)
        continue;

      image[j * width + i] |= glyphBitmap->buffer[q * glyphBitmap->width + p];
    }
  }
}

// see: https://en.wikipedia.org/wiki/Netpbm_format#PGM_example
static void
PrintPGM(unsigned char *image, FT_Int width, FT_Int height)
{
  printf("P2\n");
  printf("%d %d\n", width, height);
  printf("255\n");
  
  for (FT_Int i = 0; i < height; i++ )
  {
    for (FT_Int j = 0; j < width; j++ )
    {
      printf("%d ", image[i * width + j]);
    }
    putchar('\n');
  }
}


int
main(int argc, char **argv)
{
  FT_Library library;
  FT_Face face;

  static const auto fontSize = 40;
  static const auto resolutionDpi = 72;

  if (argc != 2)
  {
    fprintf(stderr, "usage: %s font\n", argv[0]);
    return -1;
  }

  auto fontFilename = argv[1];

  if (FT_Init_FreeType(&library) != FT_Err_Ok)
  {
    fprintf(stderr, "Could not initialize FreeType\n");
    return -1;
  }

  if (FT_New_Face(library, fontFilename, 0, &face) != FT_Err_Ok)
  {
    fprintf(stderr, "Could not load the font '%s'\n", fontFilename);
    return -1;
  }

  FT_Set_Char_Size(face, fontSize * 64, 0, resolutionDpi, 0);

  ///////////////////////////////////////////////////////////////
  
  static const std::string str = u8""
    //"The quick brown [fox] jumps over the lazy dog? 123"
    "قد ماتَ قـومٌ ومَا مَاتَتْ مـكـارِمُهم        وعَاشَ قومٌ وهُم فِي النَّاس ِأمْواتُ"
    //"أهلاً بالعالم 123"
    //"█عربي█"
    //"♥ أهلا ♥ Hello ♥"
    //"ABCD أبجد EFGH"
    //"أبجد ABCD هوز"
  ;

  ///////////////////////////////////////////////////////////////
  // ICU reordering
  ///////////////////////////////////////////////////////////////

  UErrorCode error = U_ZERO_ERROR;
  UConverter *converter = ucnv_open("UTF-8", &error);
  // Convert from utf-8 to utf-16
  size_t length;
  UChar *utf16str = new UChar[str.length() + 1]();
  length = ucnv_toUChars(converter, utf16str, str.length() + 1, str.c_str(), -1, &error);
  UChar *utf16strReordered = new UChar[length + 1]();

  // Reorder string into visual order
  UBiDi* bidi = ubidi_openSized(length + 1, 0, &error);
  ubidi_setPara(bidi, utf16str, length, HB_DIRECTION_LTR, NULL, &error);

  length = ubidi_writeReordered(bidi, utf16strReordered, length + 1, UBIDI_DO_MIRRORING , &error);
  ubidi_close(bidi);

  // Convert back from utf-16 to utf-8
  std::vector<char> utf8str(length * 2);
  length = ucnv_fromUChars(converter, utf8str.data(), utf8str.size(), utf16strReordered, length, &error);
  utf8str.resize(length);

  // Delete the no longer needed utf16 buffers
  delete[] utf16str;
  delete[] utf16strReordered;

  ucnv_close(converter);

  ///////////////////////////////////////////////////////////////
  // Harfbuzz shaping
  ///////////////////////////////////////////////////////////////

  hb_font_t* hb_font = hb_ft_font_create(face, nullptr);
  hb_buffer_t *buffer = hb_buffer_create();
  // Configure the buffer
  hb_buffer_add_utf8(buffer, utf8str.data(), utf8str.size(), 0, -1);
  hb_buffer_set_direction(buffer, HB_DIRECTION_LTR);
  hb_buffer_set_script(buffer, HB_SCRIPT_ARABIC);

  // Perform shaping
  hb_shape(hb_font, buffer, nullptr, 0);

  // Extract shaped glyphs information
  unsigned int glyphCount;
  hb_glyph_info_t *glyphInfos = hb_buffer_get_glyph_infos(buffer, &glyphCount);

  std::vector<hb_codepoint_t> glyphCodepoints(glyphCount);
  for (auto i = 0U ; i < glyphCount ; ++i)
  {
    glyphCodepoints[i] = glyphInfos[i].codepoint;
  }

  hb_buffer_destroy(buffer);

  ///////////////////////////////////////////////////////////////
  // FreeType rendering
  ///////////////////////////////////////////////////////////////

  // Shorthand
  FT_GlyphSlot glyphSlot = face->glyph;

  // Compute the maximum needed shift above the origin
  // see: https://www.freetype.org/freetype2/docs/tutorial/step2.html#section-1
  int aboveOriginShift = INT_MIN;

  // Compute the minimum bounding box that contains all glyphs in the text
  FT_Vector translation;
  translation.x = 0;
  translation.y = 0;

  FT_BBox bbox;
  bbox.xMin = bbox.yMin =  INT_MAX;
  bbox.xMax = bbox.yMax = INT_MIN;

  for (auto codepoint : glyphCodepoints)
  {
    FT_BBox glyphBbox;

    FT_Set_Transform(face, NULL, &translation);
    FT_Load_Glyph(face, codepoint, FT_LOAD_RENDER);
    FT_Glyph glyph;
    FT_Get_Glyph(glyphSlot, &glyph);
    FT_Glyph_Get_CBox(glyph, ft_glyph_bbox_subpixels, &glyphBbox );
    FT_Done_Glyph(glyph);

    translation.x += glyphSlot->advance.x;
    translation.y += glyphSlot->advance.y;

    if (glyphBbox.xMin < bbox.xMin) bbox.xMin = glyphBbox.xMin;
    if (glyphBbox.yMin < bbox.yMin) bbox.yMin = glyphBbox.yMin;
    if (glyphBbox.xMax > bbox.xMax) bbox.xMax = glyphBbox.xMax;
    if (glyphBbox.yMax > bbox.yMax) bbox.yMax = glyphBbox.yMax;

    int temp = glyphSlot->metrics.height - glyphSlot->metrics.horiBearingY;
    if (temp > aboveOriginShift) aboveOriginShift = temp;
  }

  // Construct the image
  FT_Int width = (bbox.xMax - bbox.xMin) / 64;
  FT_Int height = (bbox.yMax - bbox.yMin) / 64;

  unsigned char *image = new unsigned char[width*height]();

  // Draw glyph
  translation.x = 0;
  translation.y = aboveOriginShift;
  for (auto codepoint : glyphCodepoints)
  {
    FT_Set_Transform(face, NULL, &translation);
    FT_Load_Glyph(face, codepoint, FT_LOAD_RENDER);

    DrawGlyph(image, width, height,
        &glyphSlot->bitmap, glyphSlot->bitmap_left, height - glyphSlot->bitmap_top);

    translation.x += glyphSlot->advance.x;
    translation.y += glyphSlot->advance.y;
  }

  // Print the image
  PrintPGM(image, width, height);

  delete[] image;

  FT_Done_Face(face);
  FT_Done_FreeType(library);

  return 0;
}
