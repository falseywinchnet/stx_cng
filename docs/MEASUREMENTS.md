# Measurements

Every number on the published pages comes from the records in [`evidence/cng-1.0`](../evidence/cng-1.0). The corpus is 236
images: 24 photographs (Kodak), 14 screen captures, 10 smooth graphics, 13 mixed images, 23 noise images, 96
Noto Emoji at 128 px and 56 small synthetic tile patterns. The images themselves are not distributed.

## Exactness

Every file decodes exactly to its samples, in CNG's B, G, R, A order, at one and four threads (236 of 236 corpus files, 20 of 20 vectors), its mosaic decodes to the
block means, and the library's encoder writes the Python reference's files byte for byte (236 of 236).

## Size

Complete files against OptiPNG 7.9.1 at `-o7` with metadata stripped, and against lossless WebP (libwebp, method 6,
`exact`) for reference.

| Image set | CNG bytes | OptiPNG bytes | CNG / OptiPNG | Smaller than OptiPNG | WebP / OptiPNG |
| --- | ---: | ---: | ---: | ---: | ---: |
| Photographs (24) | 11,862,866 | 15,247,591 | 0.778 | 24 of 24 | 0.735 |
| Screen captures (14) | 3,296,292 | 4,334,415 | 0.760 | 13 of 14 | 0.657 |
| Smooth graphics (10) | 182,153 | 188,021 | 0.969 | 6 of 10 | 0.825 |
| Mixed photographic and graphic content (13) | 2,292,300 | 2,547,324 | 0.900 | 6 of 13 | 0.823 |
| Noise (23) | 1,536,154 | 1,451,681 | 1.058 | 1 of 23 | 0.964 |
| 128 px emoji (96) | 614,479 | 502,050 | 1.224 | 5 of 96 | 0.795 |
| Small periodic tile patterns (56) | 313,311 | 150,455 | 2.082 | 0 of 56 | 0.879 |
| All (236) | 20,097,555 | 24,421,537 | 0.823 | 55 of 236 | 0.747 |

## Time

Median times from memory into one buffer on a Mac mini with an Apple M4 (macOS 26.5, arm64), summed over each set,
in milliseconds. CNG is the library's codec sources built with clang at -O3 and libzstd 1.5.7; PNG is libpng
decoding a PNG level 9 file and Pillow (libpng and zlib) encoding at level 9;
WebP is libwebp decoding a lossless file.

| Image set | CNG decode, 1 thread | CNG decode, 4 threads | PNG decode | WebP decode | CNG encode, 1 thread | CNG encode, 4 threads | PNG level 9 encode |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Photographs (24) | 316.6 | 123.4 | 87.3 | 130.8 | 6,782.2 | 2,222.4 | 1,533.1 |
| Screen captures (14) | 125.7 | 48.5 | 49.2 | 50.3 | 8,199.8 | 2,552.7 | 1,112.1 |
| Smooth graphics (10) | 9.6 | 4.0 | 3.0 | 3.7 | 1,798.3 | 604.1 | 338.0 |
| Mixed photographic and graphic content (13) | 55.3 | 22.9 | 17.0 | 21.5 | 2,822.6 | 945.1 | 420.6 |
| Noise (23) | 26.6 | 11.5 | 8.3 | 11.2 | 916.7 | 328.4 | 201.4 |
| 128 px emoji (96) | 19.6 | 16.8 | 7.2 | 7.1 | 3,337.2 | 1,445.0 | 405.2 |
| Small periodic tile patterns (56) | 20.9 | 10.8 | 8.5 | 5.4 | 723.9 | 305.6 | 84.5 |
| All (236) | 574.5 | 237.9 | 180.4 | 229.9 | 24,580.8 | 8,403.4 | 4,094.9 |

## Per image

| Image | Size | CNG bytes | OptiPNG bytes | CNG decode µs, 1 thread | PNG decode µs |
| --- | --- | ---: | ---: | ---: | ---: |
| research_structured/checker | 67 × 65 × 3 | 464 | 114 | 63 | 12 |
| research_noisy/noise2 | 128 × 128 × 3 | 22,036 | 20,229 | 359 | 206 |
| research_noisy/noise32 | 128 × 128 × 3 | 45,419 | 40,923 | 406 | 278 |
| research_noisy/noise1 | 128 × 128 × 3 | 17,196 | 16,639 | 530 | 190 |
| research_noisy/noise4 | 128 × 128 × 3 | 27,298 | 24,570 | 368 | 201 |
| research_noisy/noise8 | 128 × 128 × 3 | 33,107 | 29,508 | 556 | 217 |
| research_smooth/ramp | 128 × 128 × 3 | 368 | 147 | 85 | 34 |
| research_mixed/half_noise | 128 × 128 × 3 | 30,660 | 31,472 | 433 | 205 |
| research_noisy/random | 67 × 65 × 3 | 13,961 | 13,198 | 85 | 8 |
| research_noisy/retained_random_rgb | 67 × 63 × 3 | 13,494 | 12,794 | 81 | 8 |
| research_photo/photo | 512 × 512 × 3 | 410,297 | 451,781 | 8,799 | 2,370 |
| research_noisy/retained_smooth_noise_one | 512 × 512 × 3 | 239,914 | 243,200 | 9,828 | 2,276 |
| research_noisy/speckles25 | 128 × 128 × 3 | 37,307 | 32,911 | 478 | 188 |
| research_noisy/speckles5 | 128 × 128 × 3 | 18,506 | 16,813 | 425 | 92 |
| research_noisy/speckles50 | 128 × 128 × 3 | 47,248 | 44,129 | 360 | 214 |
| research_noisy/speckles90 | 128 × 128 × 3 | 50,577 | 49,353 | 300 | 22 |
| research_smooth/surface | 128 × 128 × 3 | 9,119 | 10,496 | 266 | 118 |
| research_smooth/surface_64_noise_0 | 64 × 64 × 3 | 3,402 | 3,741 | 108 | 40 |
| research_noisy/surface_64_noise_2 | 64 × 64 × 3 | 6,166 | 5,457 | 126 | 88 |
| research_noisy/surface_64_noise_8 | 64 × 64 × 3 | 9,005 | 7,717 | 98 | 65 |
| research_noisy/surface_64_noise_32 | 64 × 64 × 3 | 12,054 | 10,381 | 106 | 84 |
| research_smooth/surface_128_noise_0 | 128 × 128 × 3 | 8,951 | 10,451 | 298 | 111 |
| research_noisy/surface_128_noise_2 | 128 × 128 × 3 | 21,773 | 19,994 | 398 | 216 |
| research_smooth/retained_smooth_transposed | 512 × 512 × 3 | 61,651 | 70,756 | 3,233 | 762 |
| research_noisy/surface_128_noise_8 | 128 × 128 × 3 | 33,026 | 29,367 | 394 | 214 |
| research_noisy/surface_128_noise_32 | 128 × 128 × 3 | 45,461 | 40,952 | 412 | 247 |
| research_smooth/retained_smooth_half | 512 × 512 × 3 | 52,963 | 52,905 | 3,330 | 1,002 |
| research_noisy/surface_256_noise_8 | 256 × 256 × 3 | 128,554 | 115,640 | 2,547 | 795 |
| research_noisy/surface_256_noise_2 | 256 × 256 × 3 | 82,304 | 75,042 | 2,063 | 511 |
| research_mixed/strip_8_axis_0 | 128 × 128 × 3 | 47,929 | 46,341 | 288 | 68 |
| research_mixed/strip_8_axis_1 | 128 × 128 × 3 | 47,989 | 46,577 | 330 | 237 |
| research_noisy/surface_256_noise_32 | 256 × 256 × 3 | 177,974 | 162,801 | 2,252 | 924 |
| research_mixed/strip_16_axis_0 | 128 × 128 × 3 | 44,929 | 43,288 | 321 | 60 |
| research_mixed/strip_16_axis_1 | 128 × 128 × 3 | 44,978 | 43,551 | 280 | 176 |
| research_mixed/strip_24_axis_0 | 128 × 128 × 3 | 41,669 | 40,234 | 259 | 48 |
| research_smooth/odd_surface_65_67 | 67 × 65 × 3 | 3,958 | 4,041 | 125 | 44 |
| research_smooth/odd_surface_31_19 | 19 × 31 × 3 | 1,110 | 939 | 38 | 11 |
| research_mixed/strip_24_axis_1 | 128 × 128 × 3 | 41,958 | 40,511 | 269 | 202 |
| research_smooth/surface_256_noise_0 | 256 × 256 × 3 | 25,526 | 26,771 | 1,363 | 411 |
| archive_noise_mixed/random | 384 × 256 × 3 | 299,676 | 295,276 | 2,310 | 643 |
| archive_structured/nyquist_checker | 384 × 256 × 3 | 1,078 | 148 | 359 | 81 |
| archive_smooth/smooth | 384 × 256 × 3 | 15,105 | 7,774 | 799 | 424 |
| research_mixed/retained_whole | 1024 × 512 × 3 | 469,002 | 548,459 | 13,835 | 3,889 |
| archive_noise_mixed/mixed_order_noise | 384 × 256 × 3 | 154,098 | 144,787 | 2,163 | 578 |
| archive_photographs/kodim01 | 768 × 512 × 3 | 531,308 | 720,817 | 11,749 | 3,625 |
| archive_photographs/kodim02 | 768 × 512 × 3 | 485,277 | 616,979 | 14,939 | 3,722 |
| archive_photographs/kodim04 | 512 × 768 × 3 | 486,952 | 631,043 | 13,672 | 4,198 |
| archive_photographs/kodim03 | 768 × 512 × 3 | 389,160 | 502,345 | 13,569 | 2,792 |
| archive_photographs/kodim05 | 768 × 512 × 3 | 592,471 | 773,505 | 12,203 | 3,440 |
| archive_photographs/kodim06 | 768 × 512 × 3 | 492,150 | 618,625 | 13,104 | 2,961 |
| archive_photographs/kodim07 | 768 × 512 × 3 | 422,104 | 557,242 | 13,899 | 3,929 |
| archive_photographs/kodim08 | 768 × 512 × 3 | 575,244 | 758,295 | 14,899 | 4,775 |
| archive_photographs/kodim09 | 512 × 768 × 3 | 450,514 | 579,851 | 11,736 | 3,332 |
| archive_photographs/kodim10 | 512 × 768 × 3 | 463,808 | 592,726 | 13,560 | 3,747 |
| archive_photographs/kodim11 | 768 × 512 × 3 | 479,164 | 620,534 | 13,773 | 3,333 |
| archive_photographs/kodim12 | 768 × 512 × 3 | 420,065 | 530,947 | 12,528 | 3,405 |
| archive_photographs/kodim13 | 768 × 512 × 3 | 636,297 | 821,757 | 11,711 | 3,882 |
| archive_photographs/kodim14 | 768 × 512 × 3 | 542,434 | 691,419 | 14,455 | 4,176 |
| archive_photographs/kodim16 | 768 × 512 × 3 | 430,630 | 533,985 | 12,880 | 3,324 |
| archive_photographs/kodim15 | 768 × 512 × 3 | 464,811 | 599,367 | 13,180 | 3,299 |
| archive_photographs/kodim17 | 512 × 768 × 3 | 464,388 | 602,005 | 11,722 | 3,389 |
| archive_photographs/kodim18 | 512 × 768 × 3 | 598,226 | 766,279 | 13,866 | 4,479 |
| archive_photographs/kodim19 | 512 × 768 × 3 | 512,127 | 659,416 | 14,237 | 3,731 |
| archive_photographs/kodim21 | 768 × 512 × 3 | 511,517 | 636,485 | 11,717 | 3,062 |
| archive_photographs/kodim22 | 768 × 512 × 3 | 551,670 | 700,564 | 13,393 | 3,676 |
| archive_structured/vertical_1 | 384 × 256 × 3 | 1,036 | 138 | 267 | 112 |
| archive_photographs/kodim20 | 768 × 512 × 3 | 384,319 | 492,361 | 10,104 | 2,766 |
| archive_structured/horizontal_1 | 384 × 256 × 3 | 1,073 | 112 | 231 | 100 |
| archive_structured/vertical_7 | 384 × 256 × 3 | 1,036 | 139 | 271 | 105 |
| archive_structured/horizontal_7 | 384 × 256 × 3 | 1,064 | 116 | 267 | 108 |
| archive_structured/vertical_8 | 384 × 256 × 3 | 1,036 | 138 | 265 | 98 |
| archive_structured/horizontal_8 | 384 × 256 × 3 | 1,064 | 117 | 260 | 113 |
| archive_structured/vertical_15 | 384 × 256 × 3 | 1,038 | 139 | 269 | 100 |
| archive_structured/horizontal_15 | 384 × 256 × 3 | 1,070 | 117 | 271 | 119 |
| archive_structured/vertical_16 | 384 × 256 × 3 | 1,024 | 139 | 270 | 130 |
| archive_structured/horizontal_16 | 384 × 256 × 3 | 1,052 | 117 | 268 | 106 |
| archive_structured/vertical_31 | 384 × 256 × 3 | 1,044 | 140 | 272 | 116 |
| archive_structured/horizontal_31 | 384 × 256 × 3 | 1,060 | 118 | 274 | 116 |
| archive_structured/vertical_32 | 384 × 256 × 3 | 1,024 | 139 | 268 | 109 |
| archive_structured/horizontal_32 | 384 × 256 × 3 | 1,012 | 119 | 266 | 114 |
| archive_structured/vertical_33 | 384 × 256 × 3 | 1,030 | 140 | 265 | 108 |
| archive_structured/horizontal_33 | 384 × 256 × 3 | 1,074 | 118 | 257 | 114 |
| archive_structured/vertical_64 | 384 × 256 × 3 | 1,014 | 140 | 266 | 114 |
| archive_structured/horizontal_64 | 384 × 256 × 3 | 1,008 | 121 | 661 | 115 |
| archive_photographs/kodim23 | 768 × 512 × 3 | 459,468 | 556,792 | 17,459 | 4,664 |
| archive_structured/vertical_127 | 384 × 256 × 3 | 1,072 | 140 | 268 | 106 |
| archive_structured/horizontal_127 | 384 × 256 × 3 | 1,046 | 127 | 267 | 119 |
| archive_structured/vertical_128 | 384 × 256 × 3 | 1,006 | 139 | 308 | 112 |
| archive_structured/horizontal_128 | 384 × 256 × 3 | 994 | 127 | 266 | 110 |
| archive_structured/diagonal_half | 384 × 256 × 3 | 1,161 | 417 | 336 | 129 |
| archive_structured/diagonal_one | 384 × 256 × 3 | 1,110 | 532 | 311 | 102 |
| archive_structured/oblique_3_7 | 384 × 256 × 3 | 1,331 | 473 | 287 | 118 |
| archive_structured/thin_horizontal | 384 × 256 × 3 | 1,027 | 143 | 254 | 100 |
| archive_structured/thin_diagonal | 384 × 256 × 3 | 1,149 | 520 | 287 | 128 |
| archive_structured/rectangle_aligned | 384 × 256 × 3 | 1,051 | 143 | 263 | 96 |
| archive_structured/rectangle_unaligned | 384 × 256 × 3 | 1,216 | 147 | 269 | 101 |
| archive_structured/drawing_rgb | 384 × 256 × 3 | 1,578 | 223 | 288 | 124 |
| archive_photographs/kodim24 | 768 × 512 × 3 | 518,762 | 684,252 | 12,273 | 3,593 |
| archive_structured/rectangle_on_gradient | 384 × 256 × 3 | 1,846 | 1,238 | 350 | 397 |
| archive_structured/checker_phase_1 | 384 × 256 × 3 | 1,032 | 148 | 315 | 80 |
| archive_structured/thin_blueprint | 384 × 256 × 3 | 1,701 | 726 | 320 | 311 |
| archive_structured/tile_2_phase_0 | 384 × 256 × 3 | 1,226 | 208 | 369 | 241 |
| archive_structured/oblique_periodic | 384 × 256 × 3 | 1,814 | 403 | 361 | 150 |
| archive_structured/tile_2_phase_1 | 384 × 256 × 3 | 1,202 | 208 | 439 | 321 |
| archive_structured/tile_2_phase_break | 384 × 256 × 3 | 1,356 | 222 | 380 | 247 |
| archive_structured/tile_3_phase_0 | 384 × 256 × 3 | 2,188 | 381 | 347 | 222 |
| archive_structured/rectangle_on_texture | 384 × 256 × 3 | 141,963 | 126,527 | 3,440 | 808 |
| archive_structured/tile_3_phase_1 | 384 × 256 × 3 | 2,204 | 381 | 382 | 222 |
| archive_structured/tile_3_phase_break | 384 × 256 × 3 | 2,329 | 397 | 350 | 222 |
| archive_structured/tile_5_phase_0 | 384 × 256 × 3 | 3,511 | 599 | 353 | 139 |
| archive_structured/tile_5_phase_1 | 384 × 256 × 3 | 3,517 | 600 | 342 | 127 |
| archive_structured/tile_5_phase_break | 384 × 256 × 3 | 3,729 | 638 | 347 | 127 |
| archive_structured/tile_7_phase_0 | 384 × 256 × 3 | 5,465 | 734 | 352 | 97 |
| archive_structured/tile_7_phase_1 | 384 × 256 × 3 | 5,446 | 734 | 358 | 98 |
| archive_structured/tile_7_phase_break | 384 × 256 × 3 | 5,670 | 775 | 357 | 110 |
| archive_structured/tile_8_phase_0 | 384 × 256 × 3 | 5,284 | 823 | 350 | 186 |
| archive_structured/tile_8_phase_1 | 384 × 256 × 3 | 5,558 | 823 | 343 | 187 |
| archive_structured/tile_8_phase_break | 384 × 256 × 3 | 5,664 | 852 | 370 | 214 |
| archive_structured/tile_16_phase_0 | 384 × 256 × 3 | 19,988 | 1,728 | 433 | 128 |
| archive_structured/tile_16_phase_1 | 384 × 256 × 3 | 19,936 | 1,729 | 417 | 124 |
| archive_structured/tile_16_phase_break | 384 × 256 × 3 | 20,252 | 1,808 | 424 | 128 |
| archive_structured/ramp_plus_tile7 | 384 × 256 × 3 | 16,388 | 2,073 | 450 | 179 |
| archive_composite/composite_photograph_crops | 64 × 2048 × 3 | 180,627 | 201,965 | 4,595 | 2,545 |
| reference_fixture/dog-half | 512 × 512 × 3 | 410,297 | 451,781 | 10,036 | 2,683 |
| guiforms_capture/correspondence-macos | 1120 × 720 × 4 | 48,074 | 47,306 | 3,023 | 1,834 |
| guiforms_capture/anchored_popup_layer | 755 × 370 × 3 | 116,714 | 153,716 | 4,705 | 1,536 |
| reference_fixture/smooth-half | 512 × 512 × 3 | 52,963 | 52,905 | 3,884 | 1,010 |
| guiforms_capture/gallery-macos | 1280 × 820 × 4 | 119,271 | 123,639 | 5,007 | 3,377 |
| guiforms_capture/button_family | 620 × 96 × 3 | 20,134 | 24,385 | 548 | 233 |
| reference_fixture/reference | 1024 × 512 × 3 | 469,002 | 548,459 | 11,991 | 3,486 |
| guiforms_capture/card_collection | 204 × 392 × 3 | 46,678 | 64,739 | 1,380 | 579 |
| guiforms_capture/check_box | 305 × 146 × 3 | 12,905 | 15,541 | 417 | 203 |
| guiforms_capture/basic_controls_overview | 690 × 430 × 3 | 84,818 | 116,365 | 3,850 | 1,578 |
| guiforms_capture/checked_list_box | 840 × 558 × 3 | 154,246 | 209,646 | 5,533 | 2,435 |
| guiforms_capture/basic_controls_002_screen | 1259 × 760 × 3 | 530,888 | 704,429 | 20,173 | 6,534 |
| guiforms_capture/checked_list_box_screen | 1259 × 760 × 3 | 520,493 | 691,699 | 17,331 | 6,754 |
| guiforms_capture/collection_menu_screen | 1259 × 760 × 3 | 472,808 | 622,722 | 17,453 | 7,542 |
| noto_faces/emoji_u1f600.png | 128 × 128 × 4 | 7,638 | 6,277 | 241 | 67 |
| noto_faces/emoji_u1f601.png | 128 × 128 × 4 | 7,476 | 6,149 | 233 | 69 |
| noto_faces/emoji_u1f602.png | 128 × 128 × 4 | 8,482 | 6,993 | 214 | 73 |
| noto_faces/emoji_u1f603.png | 128 × 128 × 4 | 7,576 | 6,323 | 245 | 90 |
| noto_faces/emoji_u1f604.png | 128 × 128 × 4 | 7,499 | 6,106 | 220 | 65 |
| noto_faces/emoji_u1f605.png | 128 × 128 × 4 | 7,882 | 6,702 | 275 | 78 |
| noto_faces/emoji_u1f606.png | 128 × 128 × 4 | 7,713 | 6,289 | 219 | 75 |
| noto_faces/emoji_u1f607.png | 128 × 128 × 4 | 7,385 | 5,963 | 285 | 59 |
| guiforms_capture/container_focus | 840 × 558 × 3 | 138,242 | 188,176 | 5,166 | 2,390 |
| noto_faces/emoji_u1f608.png | 128 × 128 × 4 | 8,970 | 8,444 | 243 | 73 |
| noto_faces/emoji_u1f609.png | 128 × 128 × 4 | 7,097 | 5,910 | 225 | 69 |
| guiforms_capture/composition_bundle_001_screen | 1259 × 760 × 3 | 526,590 | 703,006 | 19,559 | 7,039 |
| noto_faces/emoji_u1f60a.png | 128 × 128 × 4 | 8,037 | 8,233 | 227 | 73 |
| noto_faces/emoji_u1f60b.png | 128 × 128 × 4 | 7,522 | 6,075 | 268 | 59 |
| noto_faces/emoji_u1f60c.png | 128 × 128 × 4 | 7,085 | 5,894 | 258 | 63 |
| noto_faces/emoji_u1f60d.png | 128 × 128 × 4 | 8,336 | 7,159 | 209 | 71 |
| noto_faces/emoji_u1f60e.png | 128 × 128 × 4 | 6,617 | 5,732 | 220 | 65 |
| noto_faces/emoji_u1f60f.png | 128 × 128 × 4 | 7,237 | 6,027 | 202 | 65 |
| noto_faces/emoji_u1f610.png | 128 × 128 × 4 | 6,582 | 5,456 | 258 | 62 |
| noto_faces/emoji_u1f611.png | 128 × 128 × 4 | 6,166 | 5,085 | 200 | 53 |
| noto_faces/emoji_u1f612.png | 128 × 128 × 4 | 7,233 | 6,045 | 205 | 65 |
| noto_faces/emoji_u1f613.png | 128 × 128 × 4 | 7,110 | 5,960 | 272 | 61 |
| noto_faces/emoji_u1f614.png | 128 × 128 × 4 | 7,001 | 5,850 | 217 | 60 |
| noto_faces/emoji_u1f615.png | 128 × 128 × 4 | 6,885 | 5,741 | 244 | 65 |
| noto_faces/emoji_u1f616.png | 128 × 128 × 4 | 7,252 | 5,949 | 223 | 69 |
| noto_faces/emoji_u1f617.png | 128 × 128 × 4 | 6,876 | 5,694 | 208 | 59 |
| noto_faces/emoji_u1f618.png | 128 × 128 × 4 | 7,773 | 6,437 | 299 | 89 |
| noto_faces/emoji_u1f619.png | 128 × 128 × 4 | 6,819 | 5,614 | 204 | 59 |
| noto_faces/emoji_u1f61a.png | 128 × 128 × 4 | 8,454 | 8,756 | 268 | 82 |
| noto_faces/emoji_u1f61b.png | 128 × 128 × 4 | 6,950 | 5,552 | 213 | 60 |
| noto_faces/emoji_u1f61c.png | 128 × 128 × 4 | 7,391 | 5,925 | 217 | 68 |
| noto_faces/emoji_u1f61d.png | 128 × 128 × 4 | 7,218 | 5,722 | 216 | 68 |
| noto_faces/emoji_u1f61e.png | 128 × 128 × 4 | 6,619 | 5,498 | 208 | 60 |
| noto_mixed/emoji_u1f3cb_1f3fb.png | 128 × 128 × 4 | 5,467 | 4,756 | 194 | 65 |
| noto_faces/emoji_u1f61f.png | 128 × 128 × 4 | 7,189 | 5,984 | 204 | 60 |
| noto_mixed/emoji_u270b_1f3fe.png | 128 × 128 × 4 | 3,944 | 2,055 | 156 | 62 |
| noto_mixed/emoji_u1f431.png | 128 × 128 × 4 | 4,417 | 2,334 | 145 | 58 |
| noto_mixed/emoji_u1f469_1f3ff_200d_1f91d_200d_1f468_1f3fc.png | 128 × 128 × 4 | 6,703 | 6,027 | 307 | 73 |
| guiforms_capture/container_bundle_003_screen | 1259 × 760 × 3 | 504,431 | 669,046 | 21,568 | 7,165 |
| noto_mixed/emoji_u1f46e_1f3fd_200d_2642.png | 128 × 128 × 4 | 6,459 | 5,106 | 194 | 89 |
| noto_mixed/emoji_u1fab6.png | 128 × 128 × 4 | 6,932 | 6,595 | 210 | 78 |
| noto_mixed/emoji_u1f44d_1f3fc.png | 128 × 128 × 4 | 4,431 | 2,293 | 168 | 71 |
| noto_mixed/emoji_u1f468_1f3fe_200d_2764_200d_1f468_1f3fc.png | 128 × 128 × 4 | 7,159 | 6,063 | 195 | 68 |
| noto_mixed/emoji_u1f4bd.png | 128 × 128 × 4 | 8,245 | 9,144 | 274 | 125 |
| noto_mixed/emoji_u1f470_1f3ff_200d_2640.png | 128 × 128 × 4 | 6,564 | 5,914 | 188 | 83 |
| noto_mixed/emoji_u1f9e9.png | 128 × 128 × 4 | 6,603 | 5,956 | 201 | 128 |
| noto_mixed/emoji_u1fa90.png | 128 × 128 × 4 | 6,818 | 5,340 | 182 | 95 |
| noto_mixed/emoji_u1faf1_1f3fe.png | 128 × 128 × 4 | 3,656 | 1,754 | 139 | 123 |
| noto_mixed/emoji_u1f481_1f3fb_200d_2640.png | 128 × 128 × 4 | 5,275 | 4,882 | 364 | 126 |
| noto_mixed/emoji_u1f9d1_1f3fb_200d_1f384.png | 128 × 128 × 4 | 6,139 | 4,907 | 203 | 74 |
| noto_mixed/emoji_u1f3c3_1f3fe_200d_2640.png | 128 × 128 × 4 | 4,870 | 3,851 | 148 | 65 |
| noto_mixed/emoji_u1f552.png | 128 × 128 × 4 | 4,684 | 2,086 | 175 | 53 |
| noto_mixed/emoji_u1f468_200d_2764_200d_1f468.png | 128 × 128 × 4 | 7,143 | 5,409 | 189 | 67 |
| noto_mixed/emoji_u1f6d6.png | 128 × 128 × 4 | 4,429 | 2,610 | 162 | 54 |
| noto_mixed/emoji_u1f5fc.png | 128 × 128 × 4 | 2,658 | 1,560 | 149 | 52 |
| noto_mixed/emoji_u1f6b5_1f3fc_200d_2640.png | 128 × 128 × 4 | 9,464 | 8,730 | 213 | 101 |
| noto_mixed/emoji_u1f3c3_1f3fb.png | 128 × 128 × 4 | 4,578 | 3,545 | 147 | 65 |
| noto_mixed/emoji_u1faf1_1f3fe_200d_1faf2_1f3fb.png | 128 × 128 × 4 | 5,301 | 3,025 | 153 | 78 |
| noto_mixed/emoji_u1fabc.png | 128 × 128 × 4 | 10,485 | 9,660 | 239 | 92 |
| noto_mixed/emoji_u1fa85.png | 128 × 128 × 4 | 9,505 | 7,010 | 247 | 91 |
| noto_mixed/emoji_u1f47c_1f3fc.png | 128 × 128 × 4 | 6,418 | 5,166 | 184 | 67 |
| noto_mixed/emoji_u1f469_1f3fe_200d_2764_200d_1f48b_200d_1f469_1f3fe.png | 128 × 128 × 4 | 6,210 | 5,288 | 191 | 83 |
| noto_mixed/emoji_u1f9ce_1f3fc_200d_2640_200d_27a1.png | 128 × 128 × 4 | 4,687 | 3,940 | 190 | 70 |
| noto_mixed/emoji_u1f468_1f3fb_200d_2764_200d_1f48b_200d_1f468_1f3fc.png | 128 × 128 × 4 | 6,520 | 5,318 | 219 | 75 |
| noto_mixed/emoji_u1f923.png | 128 × 128 × 4 | 8,867 | 7,714 | 229 | 85 |
| noto_mixed/emoji_u270a.png | 128 × 128 × 4 | 5,696 | 2,823 | 161 | 67 |
| noto_mixed/emoji_u1f1eb.png | 128 × 128 × 4 | 795 | 259 | 70 | 37 |
| noto_mixed/emoji_u1f43d.png | 128 × 128 × 4 | 3,348 | 1,769 | 125 | 53 |
| noto_mixed/emoji_u1f469_1f3fc_200d_1f680.png | 128 × 128 × 4 | 6,478 | 5,782 | 197 | 69 |
| noto_mixed/emoji_u1f977.png | 128 × 128 × 4 | 8,053 | 8,131 | 234 | 100 |
| noto_mixed/emoji_u1f5c4.png | 128 × 128 × 4 | 2,272 | 1,185 | 102 | 69 |
| noto_mixed/emoji_u1f3a0.png | 128 × 128 × 4 | 7,365 | 5,410 | 183 | 81 |
| noto_mixed/emoji_u1f9dc_1f3fd.png | 128 × 128 × 4 | 6,408 | 5,995 | 207 | 104 |
| noto_mixed/emoji_u1f999.png | 128 × 128 × 4 | 4,336 | 2,446 | 140 | 52 |
| noto_mixed/emoji_u1f9de_200d_2642.png | 128 × 128 × 4 | 9,522 | 8,094 | 202 | 84 |
| noto_mixed/emoji_u1f468_1f3ff_200d_2764_200d_1f48b_200d_1f468_1f3fb.png | 128 × 128 × 4 | 6,137 | 5,121 | 220 | 85 |
| noto_mixed/emoji_u1f4d8.png | 128 × 128 × 4 | 1,705 | 845 | 90 | 48 |
| noto_mixed/emoji_u1f473_1f3ff_200d_2642.png | 128 × 128 × 4 | 5,139 | 3,946 | 225 | 88 |
| noto_mixed/emoji_u1f471_1f3fc.png | 128 × 128 × 4 | 5,409 | 4,515 | 170 | 69 |
| noto_mixed/emoji_u1f9d1_1f3fd_200d_1f9b0.png | 128 × 128 × 4 | 6,057 | 5,040 | 218 | 86 |
| noto_mixed/emoji_u1f574_1f3fb.png | 128 × 128 × 4 | 4,032 | 3,894 | 139 | 81 |
| noto_mixed/emoji_u1f9d7_1f3fe.png | 128 × 128 × 4 | 6,999 | 6,362 | 198 | 70 |
| noto_mixed/emoji_u1f3d6.png | 128 × 128 × 4 | 5,041 | 3,052 | 166 | 66 |
| noto_mixed/emoji_u1f486_200d_2642.png | 128 × 128 × 4 | 5,029 | 4,201 | 168 | 75 |
| noto_mixed/emoji_u1f939_1f3fb.png | 128 × 128 × 4 | 6,997 | 6,642 | 234 | 91 |
| noto_mixed/emoji_u1f5d1.png | 128 × 128 × 4 | 6,163 | 4,676 | 192 | 61 |
| noto_mixed/emoji_u1f9d9_1f3fd_200d_2640.png | 128 × 128 × 4 | 6,885 | 5,528 | 168 | 78 |
| noto_mixed/emoji_u1f93d_1f3fd_200d_2642.png | 128 × 128 × 4 | 6,768 | 5,415 | 209 | 74 |
| noto_mixed/emoji_u1f469_1f3fc_200d_2764_200d_1f469_1f3ff.png | 128 × 128 × 4 | 7,935 | 7,306 | 248 | 88 |
| noto_mixed/emoji_u1f40d.png | 128 × 128 × 4 | 4,601 | 2,568 | 157 | 66 |
| noto_mixed/emoji_u1f6b6_1f3fe_200d_2642_200d_27a1.png | 128 × 128 × 4 | 3,647 | 2,965 | 158 | 67 |
| noto_mixed/emoji_u1f93c_1f3fd_200d_2640.png | 128 × 128 × 4 | 7,963 | 7,203 | 229 | 154 |
| noto_mixed/emoji_u1f9d7_1f3fe_200d_2640.png | 128 × 128 × 4 | 7,344 | 6,557 | 207 | 73 |
| noto_mixed/emoji_u1f63e.png | 128 × 128 × 4 | 4,774 | 2,606 | 230 | 58 |
| noto_mixed/emoji_u1f959.png | 128 × 128 × 4 | 9,375 | 7,701 | 230 | 84 |
| noto_mixed/emoji_u1f469_1f3fb_200d_1f527.png | 128 × 128 × 4 | 7,140 | 6,359 | 230 | 89 |
| noto_mixed/emoji_u1f91b.png | 128 × 128 × 4 | 3,487 | 1,830 | 129 | 70 |
| noto_mixed/emoji_u1f469_1f3fd_200d_1f393.png | 128 × 128 × 4 | 6,009 | 6,068 | 190 | 171 |
| noto_mixed/emoji_u1f553.png | 128 × 128 × 4 | 4,869 | 2,174 | 166 | 54 |
