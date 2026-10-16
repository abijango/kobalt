#include "check.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <cstdio>

int main(int argc, char **argv) {
    require(argc == 3, "Expected original and mini CJK font paths");
    FT_Library library = nullptr;
    FT_Face original = nullptr, mini = nullptr;
    require(!FT_Init_FreeType(&library), "Cannot initialize pinned FreeType");
    require(!FT_New_Face(library, argv[1], 0, &original), "Cannot open original CJK outlines");
    require(!FT_New_Face(library, argv[2], 0, &mini), "Cannot open mini CJK outlines");
    require(original->num_glyphs == mini->num_glyphs && original->units_per_EM == mini->units_per_EM,
            "CJK glyph count or outline units changed");
    // Decode at one pixel per font unit with fractional coordinates retained.
    // Both loads disable hinting, so removed hints cannot affect this comparison.
    require(!FT_Set_Char_Size(original, original->units_per_EM * 64, 0, 72, 72) &&
                !FT_Set_Char_Size(mini, mini->units_per_EM * 64, 0, 72, 72),
            "Cannot set CJK outline scale");
    const FT_Int32 flags = FT_LOAD_NO_HINTING | FT_LOAD_NO_AUTOHINT | FT_LOAD_NO_BITMAP;
    for (FT_UInt glyph = 0; glyph < static_cast<FT_UInt>(original->num_glyphs); ++glyph) {
        require(!FT_Load_Glyph(original, glyph, flags) && !FT_Load_Glyph(mini, glyph, flags),
                "Cannot decode CJK glyph");
        const auto &left = original->glyph->outline, &right = mini->glyph->outline;
        bool equal = original->glyph->format == FT_GLYPH_FORMAT_OUTLINE &&
                     mini->glyph->format == FT_GLYPH_FORMAT_OUTLINE && left.n_points == right.n_points &&
                     left.n_contours == right.n_contours && left.flags == right.flags;
        for (int point = 0; equal && point < left.n_points; ++point) {
            equal = left.points[point].x == right.points[point].x && left.points[point].y == right.points[point].y &&
                    left.tags[point] == right.tags[point];
        }
        for (int contour = 0; equal && contour < left.n_contours; ++contour) {
            equal = left.contours[contour] == right.contours[contour];
        }
        if (!equal) {
            std::fprintf(stderr, "CJK outline changed at glyph %u\n", glyph);
            return 1;
        }
    }
    std::printf("Compared all %ld CJK glyph outlines with pinned FreeType\n", original->num_glyphs);
    FT_Done_Face(mini);
    FT_Done_Face(original);
    FT_Done_FreeType(library);
}
