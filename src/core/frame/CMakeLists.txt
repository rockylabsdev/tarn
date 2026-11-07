tarn_core_module()

# The 20 image gradients are compiled into the binary as Qt resources, at
# ":/tarn/backgrounds/<name>.jpg". They live with the core (not the UI module)
# because the Background kind that paints them is core code and its tests link
# only tarn_core. The JPEGs are used exactly as supplied; never re-encode them.
file(GLOB background_images CONFIGURE_DEPENDS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} assets/backgrounds/*.jpg)
qt_add_resources(tarn_core tarn_backgrounds
  PREFIX /tarn
  BASE ${CMAKE_CURRENT_SOURCE_DIR}/assets
  FILES ${background_images})
