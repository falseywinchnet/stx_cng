# Measurements

Every number on the published pages comes from the records in [`evidence/cng-1.0`](../evidence/cng-1.0). The corpus is 236
images: 24 photographs (Kodak), 14 screen captures, 10 smooth graphics, 13 mixed images, 23 noise images, 96
Noto Emoji at 128 px and 56 small synthetic tile patterns. The images themselves are not distributed.

## Exactness

Every file decodes to its samples exactly at one and four threads (236 of 236 corpus files, 20 of 20 vectors), its mosaic decodes to the
block means, and the library's encoder writes the Python reference's files byte for byte (236 of 236).

## Size

Complete files against OptiPNG 7.9.1 at `-o7` with metadata stripped, and against lossless WebP (libwebp, method 6,
`exact`) for reference.

| Image set | CNG bytes | OptiPNG bytes | CNG / OptiPNG | Smaller than OptiPNG | WebP / OptiPNG |
| --- | ---: | ---: | ---: | ---: | ---: |
| Photographs (24) | 11,872,547 | 15,247,591 | 0.779 | 24 of 24 | 0.735 |
| Screen captures (14) | 3,294,606 | 4,334,415 | 0.760 | 13 of 14 | 0.657 |
| Smooth graphics (10) | 181,679 | 188,021 | 0.966 | 7 of 10 | 0.825 |
| Mixed photographic and graphic content (13) | 2,291,825 | 2,547,324 | 0.900 | 7 of 13 | 0.823 |
| Noise (23) | 1,536,140 | 1,451,681 | 1.058 | 1 of 23 | 0.964 |
| 128 px emoji (96) | 614,222 | 502,050 | 1.223 | 5 of 96 | 0.795 |
| Small periodic tile patterns (56) | 313,380 | 150,455 | 2.083 | 0 of 56 | 0.879 |
| All (236) | 20,104,399 | 24,421,537 | 0.823 | 57 of 236 | 0.747 |

## Time

Median times from memory into one buffer on a Mac mini with an Apple M4 (macOS 26.5, arm64), summed over each set,
in milliseconds. CNG is the library's codec sources built with clang at -O3 and libzstd 1.5.7; PNG is libpng decoding a PNG level 9 file and Pillow (libpng and zlib) encoding at level 9;
WebP is libwebp decoding a lossless file.

| Image set | CNG decode, 1 thread | CNG decode, 4 threads | PNG decode | WebP decode | CNG encode, 1 thread | CNG encode, 4 threads | PNG level 9 encode |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Photographs (24) | 308.6 | 119.2 | 88.5 | 132.7 | 6,624.1 | 2,329.8 | 1,554.4 |
| Screen captures (14) | 119.5 | 43.1 | 46.7 | 48.9 | 7,745.5 | 2,511.0 | 1,028.5 |
| Smooth graphics (10) | 9.4 | 3.7 | 2.6 | 3.4 | 1,773.0 | 561.3 | 354.2 |
| Mixed photographic and graphic content (13) | 53.5 | 20.7 | 16.2 | 22.6 | 2,881.9 | 942.8 | 402.1 |
| Noise (23) | 26.3 | 12.0 | 7.9 | 11.4 | 931.7 | 307.2 | 222.3 |
| 128 px emoji (96) | 19.5 | 14.7 | 7.0 | 6.8 | 3,213.5 | 1,277.6 | 387.7 |
| Small periodic tile patterns (56) | 20.2 | 10.2 | 8.6 | 5.2 | 726.1 | 281.8 | 79.9 |
| All (236) | 557.0 | 223.6 | 177.5 | 231.1 | 23,895.7 | 8,211.5 | 4,029.0 |

## Per image

| Image | Size | CNG bytes | OptiPNG bytes | CNG decode µs, 1 thread | PNG decode µs |
| --- | --- | ---: | ---: | ---: | ---: |
| research_structured/checker | 67 × 65 × 3 | 464 | 114 | 76 | 10 |
| research_noisy/noise2 | 128 × 128 × 3 | 22,024 | 20,229 | 584 | 275 |
| research_noisy/noise32 | 128 × 128 × 3 | 45,407 | 40,923 | 310 | 243 |
| research_noisy/noise1 | 128 × 128 × 3 | 17,197 | 16,639 | 380 | 151 |
| research_noisy/noise4 | 128 × 128 × 3 | 27,282 | 24,570 | 471 | 262 |
| research_noisy/noise8 | 128 × 128 × 3 | 33,101 | 29,508 | 533 | 221 |
| research_smooth/ramp | 128 × 128 × 3 | 368 | 147 | 80 | 32 |
| research_mixed/half_noise | 128 × 128 × 3 | 30,646 | 31,472 | 384 | 150 |
| research_noisy/random | 67 × 65 × 3 | 13,959 | 13,198 | 111 | 8 |
| research_noisy/retained_random_rgb | 67 × 63 × 3 | 13,492 | 12,794 | 106 | 7 |
| research_photo/photo | 512 × 512 × 3 | 410,218 | 451,781 | 8,371 | 2,171 |
| research_noisy/retained_smooth_noise_one | 512 × 512 × 3 | 239,933 | 243,200 | 9,015 | 1,783 |
| research_noisy/speckles25 | 128 × 128 × 3 | 37,300 | 32,911 | 523 | 246 |
| research_noisy/speckles5 | 128 × 128 × 3 | 18,494 | 16,813 | 448 | 89 |
| research_noisy/speckles50 | 128 × 128 × 3 | 47,228 | 44,129 | 445 | 276 |
| research_noisy/speckles90 | 128 × 128 × 3 | 50,585 | 49,353 | 334 | 25 |
| research_smooth/surface | 128 × 128 × 3 | 9,128 | 10,496 | 287 | 110 |
| research_smooth/surface_64_noise_0 | 64 × 64 × 3 | 3,396 | 3,741 | 121 | 46 |
| research_noisy/surface_64_noise_2 | 64 × 64 × 3 | 6,168 | 5,457 | 100 | 70 |
| research_noisy/surface_64_noise_8 | 64 × 64 × 3 | 9,005 | 7,717 | 102 | 85 |
| research_noisy/surface_64_noise_32 | 64 × 64 × 3 | 12,060 | 10,381 | 121 | 80 |
| research_smooth/surface_128_noise_0 | 128 × 128 × 3 | 8,957 | 10,451 | 262 | 103 |
| research_noisy/surface_128_noise_2 | 128 × 128 × 3 | 21,773 | 19,994 | 607 | 211 |
| research_smooth/retained_smooth_transposed | 512 × 512 × 3 | 61,567 | 70,756 | 3,143 | 634 |
| research_noisy/surface_128_noise_8 | 128 × 128 × 3 | 33,027 | 29,367 | 555 | 247 |
| research_smooth/retained_smooth_half | 512 × 512 × 3 | 52,601 | 52,905 | 3,604 | 874 |
| research_noisy/surface_128_noise_32 | 128 × 128 × 3 | 45,467 | 40,952 | 328 | 253 |
| research_noisy/surface_256_noise_8 | 256 × 256 × 3 | 128,570 | 115,640 | 2,308 | 637 |
| research_noisy/surface_256_noise_2 | 256 × 256 × 3 | 82,318 | 75,042 | 2,481 | 614 |
| research_mixed/strip_8_axis_0 | 128 × 128 × 3 | 47,931 | 46,341 | 288 | 66 |
| research_mixed/strip_8_axis_1 | 128 × 128 × 3 | 47,985 | 46,577 | 327 | 186 |
| research_noisy/surface_256_noise_32 | 256 × 256 × 3 | 177,964 | 162,801 | 1,858 | 709 |
| research_mixed/strip_16_axis_0 | 128 × 128 × 3 | 44,927 | 43,288 | 310 | 60 |
| research_mixed/strip_24_axis_0 | 128 × 128 × 3 | 41,667 | 40,234 | 292 | 49 |
| research_mixed/strip_16_axis_1 | 128 × 128 × 3 | 44,972 | 43,551 | 317 | 200 |
| research_smooth/odd_surface_65_67 | 67 × 65 × 3 | 3,961 | 4,041 | 125 | 43 |
| research_smooth/odd_surface_31_19 | 19 × 31 × 3 | 1,112 | 939 | 49 | 12 |
| research_mixed/strip_24_axis_1 | 128 × 128 × 3 | 41,954 | 40,511 | 272 | 167 |
| research_smooth/surface_256_noise_0 | 256 × 256 × 3 | 25,511 | 26,771 | 1,012 | 341 |
| archive_noise_mixed/random | 384 × 256 × 3 | 299,696 | 295,276 | 2,816 | 831 |
| archive_structured/nyquist_checker | 384 × 256 × 3 | 1,078 | 148 | 314 | 81 |
| archive_smooth/smooth | 384 × 256 × 3 | 15,078 | 7,774 | 733 | 416 |
| archive_noise_mixed/mixed_order_noise | 384 × 256 × 3 | 154,090 | 144,787 | 1,807 | 578 |
| research_mixed/retained_whole | 1024 × 512 × 3 | 468,823 | 548,459 | 13,651 | 4,026 |
| archive_photographs/kodim01 | 768 × 512 × 3 | 532,138 | 720,817 | 13,789 | 4,610 |
| archive_photographs/kodim02 | 768 × 512 × 3 | 484,924 | 616,979 | 12,254 | 3,175 |
| archive_photographs/kodim04 | 512 × 768 × 3 | 486,949 | 631,043 | 13,575 | 3,427 |
| archive_photographs/kodim03 | 768 × 512 × 3 | 386,675 | 502,345 | 13,620 | 3,355 |
| archive_photographs/kodim05 | 768 × 512 × 3 | 593,310 | 773,505 | 15,275 | 4,314 |
| archive_photographs/kodim06 | 768 × 512 × 3 | 493,311 | 618,625 | 11,260 | 2,898 |
| archive_photographs/kodim07 | 768 × 512 × 3 | 421,805 | 557,242 | 14,148 | 4,243 |
| archive_photographs/kodim08 | 768 × 512 × 3 | 575,211 | 758,295 | 13,540 | 5,155 |
| archive_photographs/kodim09 | 512 × 768 × 3 | 452,441 | 579,851 | 12,307 | 3,592 |
| archive_photographs/kodim10 | 512 × 768 × 3 | 463,545 | 592,726 | 11,912 | 3,579 |
| archive_photographs/kodim11 | 768 × 512 × 3 | 479,961 | 620,534 | 13,251 | 4,127 |
| archive_photographs/kodim12 | 768 × 512 × 3 | 420,534 | 530,947 | 13,865 | 3,802 |
| archive_photographs/kodim13 | 768 × 512 × 3 | 636,513 | 821,757 | 11,995 | 3,366 |
| archive_photographs/kodim14 | 768 × 512 × 3 | 542,885 | 691,419 | 14,117 | 4,318 |
| archive_photographs/kodim16 | 768 × 512 × 3 | 430,724 | 533,985 | 12,587 | 3,088 |
| archive_photographs/kodim15 | 768 × 512 × 3 | 467,270 | 599,367 | 11,945 | 3,171 |
| archive_photographs/kodim17 | 512 × 768 × 3 | 470,747 | 602,005 | 11,787 | 3,450 |
| archive_photographs/kodim18 | 512 × 768 × 3 | 597,952 | 766,279 | 13,736 | 4,096 |
| archive_photographs/kodim19 | 512 × 768 × 3 | 511,705 | 659,416 | 12,353 | 3,709 |
| archive_photographs/kodim21 | 768 × 512 × 3 | 510,861 | 636,485 | 11,802 | 3,081 |
| archive_photographs/kodim20 | 768 × 512 × 3 | 384,979 | 492,361 | 11,282 | 2,947 |
| archive_photographs/kodim22 | 768 × 512 × 3 | 551,599 | 700,564 | 13,615 | 4,579 |
| archive_structured/vertical_1 | 384 × 256 × 3 | 1,042 | 138 | 276 | 107 |
| archive_structured/horizontal_1 | 384 × 256 × 3 | 1,081 | 112 | 285 | 113 |
| archive_structured/vertical_7 | 384 × 256 × 3 | 1,034 | 139 | 269 | 110 |
| archive_structured/horizontal_7 | 384 × 256 × 3 | 1,064 | 116 | 246 | 112 |
| archive_structured/vertical_8 | 384 × 256 × 3 | 1,034 | 138 | 271 | 124 |
| archive_structured/horizontal_8 | 384 × 256 × 3 | 1,064 | 117 | 267 | 115 |
| archive_structured/vertical_15 | 384 × 256 × 3 | 1,038 | 139 | 269 | 110 |
| archive_structured/horizontal_15 | 384 × 256 × 3 | 1,068 | 117 | 295 | 109 |
| archive_structured/vertical_16 | 384 × 256 × 3 | 1,034 | 139 | 263 | 106 |
| archive_structured/horizontal_16 | 384 × 256 × 3 | 1,054 | 117 | 259 | 103 |
| archive_structured/vertical_31 | 384 × 256 × 3 | 1,046 | 140 | 262 | 109 |
| archive_structured/horizontal_31 | 384 × 256 × 3 | 1,060 | 118 | 252 | 101 |
| archive_structured/vertical_32 | 384 × 256 × 3 | 1,024 | 139 | 260 | 95 |
| archive_structured/horizontal_32 | 384 × 256 × 3 | 1,014 | 119 | 259 | 106 |
| archive_photographs/kodim23 | 768 × 512 × 3 | 456,314 | 556,792 | 12,076 | 2,867 |
| archive_structured/vertical_33 | 384 × 256 × 3 | 1,028 | 140 | 262 | 105 |
| archive_structured/horizontal_33 | 384 × 256 × 3 | 1,078 | 118 | 261 | 106 |
| archive_structured/horizontal_64 | 384 × 256 × 3 | 1,010 | 121 | 258 | 107 |
| archive_structured/vertical_64 | 384 × 256 × 3 | 1,024 | 140 | 317 | 123 |
| archive_structured/vertical_127 | 384 × 256 × 3 | 1,066 | 140 | 270 | 117 |
| archive_structured/vertical_128 | 384 × 256 × 3 | 1,008 | 139 | 267 | 103 |
| archive_structured/horizontal_127 | 384 × 256 × 3 | 1,046 | 127 | 265 | 100 |
| archive_structured/horizontal_128 | 384 × 256 × 3 | 996 | 127 | 263 | 104 |
| archive_structured/diagonal_half | 384 × 256 × 3 | 1,165 | 417 | 294 | 124 |
| archive_structured/diagonal_one | 384 × 256 × 3 | 1,108 | 532 | 287 | 103 |
| archive_structured/oblique_3_7 | 384 × 256 × 3 | 1,329 | 473 | 293 | 133 |
| archive_structured/thin_horizontal | 384 × 256 × 3 | 1,029 | 143 | 259 | 109 |
| archive_structured/thin_diagonal | 384 × 256 × 3 | 1,153 | 520 | 362 | 130 |
| archive_structured/rectangle_aligned | 384 × 256 × 3 | 1,053 | 143 | 269 | 113 |
| archive_structured/rectangle_unaligned | 384 × 256 × 3 | 1,220 | 147 | 324 | 108 |
| archive_structured/drawing_rgb | 384 × 256 × 3 | 1,576 | 223 | 342 | 148 |
| archive_structured/rectangle_on_gradient | 384 × 256 × 3 | 1,851 | 1,238 | 352 | 414 |
| archive_structured/checker_phase_1 | 384 × 256 × 3 | 1,032 | 148 | 315 | 81 |
| archive_structured/thin_blueprint | 384 × 256 × 3 | 1,701 | 726 | 327 | 312 |
| archive_structured/oblique_periodic | 384 × 256 × 3 | 1,814 | 403 | 354 | 122 |
| archive_structured/tile_2_phase_0 | 384 × 256 × 3 | 1,226 | 208 | 367 | 242 |
| archive_photographs/kodim24 | 768 × 512 × 3 | 520,194 | 684,252 | 12,462 | 3,584 |
| archive_structured/tile_2_phase_1 | 384 × 256 × 3 | 1,202 | 208 | 368 | 239 |
| archive_structured/rectangle_on_texture | 384 × 256 × 3 | 141,988 | 126,527 | 2,937 | 797 |
| archive_structured/tile_2_phase_break | 384 × 256 × 3 | 1,350 | 222 | 381 | 251 |
| archive_structured/tile_3_phase_0 | 384 × 256 × 3 | 2,165 | 381 | 360 | 212 |
| archive_structured/tile_3_phase_1 | 384 × 256 × 3 | 2,190 | 381 | 345 | 232 |
| archive_structured/tile_3_phase_break | 384 × 256 × 3 | 2,319 | 397 | 346 | 219 |
| archive_structured/tile_5_phase_0 | 384 × 256 × 3 | 3,509 | 599 | 351 | 162 |
| archive_structured/tile_5_phase_1 | 384 × 256 × 3 | 3,511 | 600 | 347 | 141 |
| archive_structured/tile_5_phase_break | 384 × 256 × 3 | 3,737 | 638 | 404 | 142 |
| archive_structured/tile_7_phase_0 | 384 × 256 × 3 | 5,463 | 734 | 374 | 134 |
| archive_structured/tile_7_phase_1 | 384 × 256 × 3 | 5,446 | 734 | 421 | 107 |
| archive_structured/tile_8_phase_0 | 384 × 256 × 3 | 5,348 | 823 | 342 | 201 |
| archive_structured/tile_7_phase_break | 384 × 256 × 3 | 5,668 | 775 | 360 | 110 |
| archive_structured/tile_8_phase_1 | 384 × 256 × 3 | 5,516 | 823 | 339 | 196 |
| archive_structured/tile_8_phase_break | 384 × 256 × 3 | 5,670 | 852 | 348 | 205 |
| archive_structured/tile_16_phase_1 | 384 × 256 × 3 | 19,940 | 1,729 | 415 | 123 |
| archive_structured/tile_16_phase_0 | 384 × 256 × 3 | 19,990 | 1,728 | 421 | 116 |
| archive_structured/tile_16_phase_break | 384 × 256 × 3 | 20,262 | 1,808 | 430 | 154 |
| archive_structured/ramp_plus_tile7 | 384 × 256 × 3 | 16,394 | 2,073 | 452 | 212 |
| archive_composite/composite_photograph_crops | 64 × 2048 × 3 | 181,060 | 201,965 | 4,259 | 2,390 |
| reference_fixture/dog-half | 512 × 512 × 3 | 410,218 | 451,781 | 10,011 | 2,630 |
| guiforms_capture/correspondence-macos | 1120 × 720 × 4 | 48,088 | 47,306 | 2,967 | 1,885 |
| reference_fixture/smooth-half | 512 × 512 × 3 | 52,601 | 52,905 | 3,340 | 967 |
| guiforms_capture/anchored_popup_layer | 755 × 370 × 3 | 116,773 | 153,716 | 3,992 | 1,360 |
| guiforms_capture/gallery-macos | 1280 × 820 × 4 | 118,945 | 123,639 | 5,048 | 3,075 |
| guiforms_capture/button_family | 620 × 96 × 3 | 20,062 | 24,385 | 738 | 262 |
| reference_fixture/reference | 1024 × 512 × 3 | 468,823 | 548,459 | 11,698 | 3,158 |
| guiforms_capture/check_box | 305 × 146 × 3 | 12,937 | 15,541 | 364 | 180 |
| guiforms_capture/card_collection | 204 × 392 × 3 | 46,431 | 64,739 | 1,578 | 495 |
| guiforms_capture/basic_controls_overview | 690 × 430 × 3 | 84,618 | 116,365 | 3,989 | 1,566 |
| guiforms_capture/checked_list_box | 840 × 558 × 3 | 154,051 | 209,646 | 5,241 | 2,261 |
| guiforms_capture/basic_controls_002_screen | 1259 × 760 × 3 | 530,565 | 704,429 | 17,319 | 6,084 |
| guiforms_capture/checked_list_box_screen | 1259 × 760 × 3 | 520,330 | 691,699 | 19,414 | 7,292 |
| guiforms_capture/collection_menu_screen | 1259 × 760 × 3 | 472,615 | 622,722 | 15,504 | 5,693 |
| noto_faces/emoji_u1f600.png | 128 × 128 × 4 | 7,653 | 6,277 | 222 | 66 |
| noto_faces/emoji_u1f601.png | 128 × 128 × 4 | 7,476 | 6,149 | 280 | 69 |
| noto_faces/emoji_u1f602.png | 128 × 128 × 4 | 8,462 | 6,993 | 231 | 77 |
| noto_faces/emoji_u1f603.png | 128 × 128 × 4 | 7,578 | 6,323 | 213 | 68 |
| noto_faces/emoji_u1f604.png | 128 × 128 × 4 | 7,486 | 6,106 | 287 | 68 |
| noto_faces/emoji_u1f605.png | 128 × 128 × 4 | 7,895 | 6,702 | 216 | 75 |
| noto_faces/emoji_u1f606.png | 128 × 128 × 4 | 7,724 | 6,289 | 279 | 97 |
| noto_faces/emoji_u1f607.png | 128 × 128 × 4 | 7,377 | 5,963 | 220 | 71 |
| guiforms_capture/container_focus | 840 × 558 × 3 | 138,346 | 188,176 | 6,435 | 2,843 |
| guiforms_capture/composition_bundle_001_screen | 1259 × 760 × 3 | 526,626 | 703,006 | 20,111 | 7,241 |
| noto_faces/emoji_u1f608.png | 128 × 128 × 4 | 9,001 | 8,444 | 231 | 77 |
| noto_faces/emoji_u1f609.png | 128 × 128 × 4 | 7,086 | 5,910 | 268 | 74 |
| noto_faces/emoji_u1f60b.png | 128 × 128 × 4 | 7,527 | 6,075 | 216 | 58 |
| noto_faces/emoji_u1f60a.png | 128 × 128 × 4 | 7,988 | 8,233 | 222 | 70 |
| noto_faces/emoji_u1f60c.png | 128 × 128 × 4 | 7,098 | 5,894 | 215 | 64 |
| noto_faces/emoji_u1f60d.png | 128 × 128 × 4 | 8,310 | 7,159 | 215 | 72 |
| noto_faces/emoji_u1f60e.png | 128 × 128 × 4 | 6,584 | 5,732 | 259 | 67 |
| noto_faces/emoji_u1f60f.png | 128 × 128 × 4 | 7,253 | 6,027 | 203 | 80 |
| noto_faces/emoji_u1f610.png | 128 × 128 × 4 | 6,568 | 5,456 | 277 | 57 |
| noto_faces/emoji_u1f611.png | 128 × 128 × 4 | 6,169 | 5,085 | 203 | 54 |
| noto_faces/emoji_u1f612.png | 128 × 128 × 4 | 7,218 | 6,045 | 211 | 67 |
| noto_faces/emoji_u1f613.png | 128 × 128 × 4 | 7,124 | 5,960 | 243 | 61 |
| noto_faces/emoji_u1f614.png | 128 × 128 × 4 | 7,010 | 5,850 | 203 | 61 |
| noto_faces/emoji_u1f615.png | 128 × 128 × 4 | 6,880 | 5,741 | 289 | 65 |
| noto_faces/emoji_u1f617.png | 128 × 128 × 4 | 6,871 | 5,694 | 247 | 61 |
| noto_faces/emoji_u1f616.png | 128 × 128 × 4 | 7,256 | 5,949 | 274 | 68 |
| noto_faces/emoji_u1f618.png | 128 × 128 × 4 | 7,755 | 6,437 | 286 | 77 |
| noto_faces/emoji_u1f619.png | 128 × 128 × 4 | 6,789 | 5,614 | 209 | 59 |
| noto_faces/emoji_u1f61a.png | 128 × 128 × 4 | 8,422 | 8,756 | 315 | 94 |
| noto_faces/emoji_u1f61b.png | 128 × 128 × 4 | 6,918 | 5,552 | 213 | 70 |
| noto_faces/emoji_u1f61c.png | 128 × 128 × 4 | 7,361 | 5,925 | 229 | 69 |
| noto_faces/emoji_u1f61d.png | 128 × 128 × 4 | 7,224 | 5,722 | 214 | 67 |
| noto_faces/emoji_u1f61e.png | 128 × 128 × 4 | 6,645 | 5,498 | 205 | 59 |
| noto_mixed/emoji_u1f3cb_1f3fb.png | 128 × 128 × 4 | 5,470 | 4,756 | 206 | 78 |
| noto_faces/emoji_u1f61f.png | 128 × 128 × 4 | 7,193 | 5,984 | 213 | 61 |
| noto_mixed/emoji_u270b_1f3fe.png | 128 × 128 × 4 | 3,917 | 2,055 | 182 | 62 |
| noto_mixed/emoji_u1f469_1f3ff_200d_1f91d_200d_1f468_1f3fc.png | 128 × 128 × 4 | 6,687 | 6,027 | 209 | 71 |
| noto_mixed/emoji_u1f431.png | 128 × 128 × 4 | 4,418 | 2,334 | 145 | 59 |
| noto_mixed/emoji_u1fab6.png | 128 × 128 × 4 | 6,926 | 6,595 | 178 | 65 |
| noto_mixed/emoji_u1f46e_1f3fd_200d_2642.png | 128 × 128 × 4 | 6,453 | 5,106 | 178 | 80 |
| noto_mixed/emoji_u1f44d_1f3fc.png | 128 × 128 × 4 | 4,431 | 2,293 | 144 | 63 |
| guiforms_capture/container_bundle_003_screen | 1259 × 760 × 3 | 504,219 | 669,046 | 16,799 | 6,431 |
| noto_mixed/emoji_u1f468_1f3fe_200d_2764_200d_1f468_1f3fc.png | 128 × 128 × 4 | 7,144 | 6,063 | 266 | 71 |
| noto_mixed/emoji_u1f4bd.png | 128 × 128 × 4 | 8,252 | 9,144 | 225 | 104 |
| noto_mixed/emoji_u1f470_1f3ff_200d_2640.png | 128 × 128 × 4 | 6,538 | 5,914 | 183 | 82 |
| noto_mixed/emoji_u1f9e9.png | 128 × 128 × 4 | 6,591 | 5,956 | 250 | 72 |
| noto_mixed/emoji_u1fa90.png | 128 × 128 × 4 | 6,829 | 5,340 | 181 | 95 |
| noto_mixed/emoji_u1f481_1f3fb_200d_2640.png | 128 × 128 × 4 | 5,287 | 4,882 | 208 | 80 |
| noto_mixed/emoji_u1f9d1_1f3fb_200d_1f384.png | 128 × 128 × 4 | 6,159 | 4,907 | 165 | 73 |
| noto_mixed/emoji_u1faf1_1f3fe.png | 128 × 128 × 4 | 3,656 | 1,754 | 187 | 77 |
| noto_mixed/emoji_u1f3c3_1f3fe_200d_2640.png | 128 × 128 × 4 | 4,907 | 3,851 | 149 | 64 |
| noto_mixed/emoji_u1f552.png | 128 × 128 × 4 | 4,688 | 2,086 | 160 | 57 |
| noto_mixed/emoji_u1f6d6.png | 128 × 128 × 4 | 4,405 | 2,610 | 185 | 62 |
| noto_mixed/emoji_u1f5fc.png | 128 × 128 × 4 | 2,668 | 1,560 | 112 | 52 |
| noto_mixed/emoji_u1f6b5_1f3fc_200d_2640.png | 128 × 128 × 4 | 9,467 | 8,730 | 204 | 114 |
| noto_mixed/emoji_u1f468_200d_2764_200d_1f468.png | 128 × 128 × 4 | 7,133 | 5,409 | 231 | 78 |
| noto_mixed/emoji_u1f3c3_1f3fb.png | 128 × 128 × 4 | 4,566 | 3,545 | 145 | 64 |
| noto_mixed/emoji_u1faf1_1f3fe_200d_1faf2_1f3fb.png | 128 × 128 × 4 | 5,326 | 3,025 | 171 | 84 |
| noto_mixed/emoji_u1fabc.png | 128 × 128 × 4 | 10,489 | 9,660 | 228 | 92 |
| noto_mixed/emoji_u1fa85.png | 128 × 128 × 4 | 9,504 | 7,010 | 253 | 94 |
| noto_mixed/emoji_u1f47c_1f3fc.png | 128 × 128 × 4 | 6,407 | 5,166 | 183 | 63 |
| noto_mixed/emoji_u1f469_1f3fe_200d_2764_200d_1f48b_200d_1f469_1f3fe.png | 128 × 128 × 4 | 6,203 | 5,288 | 206 | 95 |
| noto_mixed/emoji_u1f468_1f3fb_200d_2764_200d_1f48b_200d_1f468_1f3fc.png | 128 × 128 × 4 | 6,502 | 5,318 | 181 | 75 |
| noto_mixed/emoji_u1f9ce_1f3fc_200d_2640_200d_27a1.png | 128 × 128 × 4 | 4,692 | 3,940 | 159 | 79 |
| noto_mixed/emoji_u1f923.png | 128 × 128 × 4 | 8,887 | 7,714 | 226 | 83 |
| noto_mixed/emoji_u270a.png | 128 × 128 × 4 | 5,696 | 2,823 | 160 | 78 |
| noto_mixed/emoji_u1f1eb.png | 128 × 128 × 4 | 801 | 259 | 69 | 39 |
| noto_mixed/emoji_u1f43d.png | 128 × 128 × 4 | 3,365 | 1,769 | 124 | 51 |
| noto_mixed/emoji_u1f469_1f3fc_200d_1f680.png | 128 × 128 × 4 | 6,487 | 5,782 | 218 | 79 |
| noto_mixed/emoji_u1f977.png | 128 × 128 × 4 | 8,060 | 8,131 | 207 | 89 |
| noto_mixed/emoji_u1f5c4.png | 128 × 128 × 4 | 2,276 | 1,185 | 117 | 77 |
| noto_mixed/emoji_u1f3a0.png | 128 × 128 × 4 | 7,368 | 5,410 | 206 | 79 |
| noto_mixed/emoji_u1f9dc_1f3fd.png | 128 × 128 × 4 | 6,414 | 5,995 | 165 | 92 |
| noto_mixed/emoji_u1f999.png | 128 × 128 × 4 | 4,360 | 2,446 | 156 | 53 |
| noto_mixed/emoji_u1f9de_200d_2642.png | 128 × 128 × 4 | 9,523 | 8,094 | 237 | 94 |
| noto_mixed/emoji_u1f468_1f3ff_200d_2764_200d_1f48b_200d_1f468_1f3fb.png | 128 × 128 × 4 | 6,158 | 5,121 | 181 | 74 |
| noto_mixed/emoji_u1f4d8.png | 128 × 128 × 4 | 1,695 | 845 | 136 | 50 |
| noto_mixed/emoji_u1f473_1f3ff_200d_2642.png | 128 × 128 × 4 | 5,136 | 3,946 | 160 | 77 |
| noto_mixed/emoji_u1f471_1f3fc.png | 128 × 128 × 4 | 5,406 | 4,515 | 171 | 72 |
| noto_mixed/emoji_u1f9d1_1f3fd_200d_1f9b0.png | 128 × 128 × 4 | 6,068 | 5,040 | 171 | 74 |
| noto_mixed/emoji_u1f574_1f3fb.png | 128 × 128 × 4 | 4,035 | 3,894 | 157 | 91 |
| noto_mixed/emoji_u1f9d7_1f3fe.png | 128 × 128 × 4 | 7,014 | 6,362 | 267 | 73 |
| noto_mixed/emoji_u1f3d6.png | 128 × 128 × 4 | 5,033 | 3,052 | 148 | 68 |
| noto_mixed/emoji_u1f486_200d_2642.png | 128 × 128 × 4 | 5,017 | 4,201 | 216 | 76 |
| noto_mixed/emoji_u1f5d1.png | 128 × 128 × 4 | 6,144 | 4,676 | 171 | 62 |
| noto_mixed/emoji_u1f939_1f3fb.png | 128 × 128 × 4 | 7,004 | 6,642 | 250 | 99 |
| noto_mixed/emoji_u1f9d9_1f3fd_200d_2640.png | 128 × 128 × 4 | 6,879 | 5,528 | 181 | 77 |
| noto_mixed/emoji_u1f93d_1f3fd_200d_2642.png | 128 × 128 × 4 | 6,767 | 5,415 | 209 | 86 |
| noto_mixed/emoji_u1f40d.png | 128 × 128 × 4 | 4,580 | 2,568 | 155 | 67 |
| noto_mixed/emoji_u1f469_1f3fc_200d_2764_200d_1f469_1f3ff.png | 128 × 128 × 4 | 7,916 | 7,306 | 284 | 89 |
| noto_mixed/emoji_u1f6b6_1f3fe_200d_2642_200d_27a1.png | 128 × 128 × 4 | 3,642 | 2,965 | 140 | 63 |
| noto_mixed/emoji_u1f93c_1f3fd_200d_2640.png | 128 × 128 × 4 | 7,907 | 7,203 | 227 | 89 |
| noto_mixed/emoji_u1f9d7_1f3fe_200d_2640.png | 128 × 128 × 4 | 7,348 | 6,557 | 193 | 71 |
| noto_mixed/emoji_u1f63e.png | 128 × 128 × 4 | 4,733 | 2,606 | 162 | 62 |
| noto_mixed/emoji_u1f959.png | 128 × 128 × 4 | 9,352 | 7,701 | 237 | 82 |
| noto_mixed/emoji_u1f469_1f3fb_200d_1f527.png | 128 × 128 × 4 | 7,158 | 6,359 | 197 | 90 |
| noto_mixed/emoji_u1f91b.png | 128 × 128 × 4 | 3,499 | 1,830 | 125 | 67 |
| noto_mixed/emoji_u1f469_1f3fd_200d_1f393.png | 128 × 128 × 4 | 6,016 | 6,068 | 177 | 84 |
| noto_mixed/emoji_u1f553.png | 128 × 128 × 4 | 4,862 | 2,174 | 195 | 52 |
