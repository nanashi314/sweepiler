# Puts the files to publish in SITE: the page, its script, wasm and data, the
# service worker, the font's license and the gzipped frames (cmake -P, from
# the web_site target).
file(REMOVE_RECURSE ${SITE})
file(MAKE_DIRECTORY ${SITE}/frames)
foreach(f index.html sweepiler.js sweepiler.wasm sweepiler.data coi-serviceworker.js OFL.txt)
  file(COPY ${BUILD}/${f} DESTINATION ${SITE})
endforeach()
file(GLOB frames ${BUILD}/frames/*.frame.gz)
file(COPY ${frames} DESTINATION ${SITE}/frames)
list(LENGTH frames count)
message(STATUS "Site in ${SITE} with ${count} prebuilt frames")
