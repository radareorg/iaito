set(TS_FILES
    translations/ar/iaito_ar.ts
    translations/ca/iaito_ca.ts
    translations/de/iaito_de.ts
    translations/es-ES/iaito_es.ts
    translations/fa/iaito_fa.ts
    translations/fr/iaito_fr.ts
    translations/he/iaito_he.ts
    translations/hi/iaito_hi.ts
    translations/it/iaito_it.ts
    translations/ja/iaito_ja.ts
    translations/nl/iaito_nl.ts
    translations/pt-PT/iaito_pt.ts
    translations/ro/iaito_ro.ts
    translations/ru/iaito_ru.ts
    translations/tr/iaito_tr.ts
    translations/zh-CN/iaito_zh.ts
)
# translations/ko/iaito_ko.ts problems with fonts
# translations/pt-BR/iaito_pt.ts #2321 handling multiple versions of a language

set_source_files_properties(${TS_FILES} PROPERTIES OUTPUT_LOCATION ${CMAKE_CURRENT_BINARY_DIR}/translations)
find_package(Qt5LinguistTools REQUIRED)
qt5_add_translation(qmFiles ${TS_FILES})
add_custom_target(translations ALL DEPENDS ${qmFiles} SOURCES ${TS_FILES})

install(FILES
    ${qmFiles}
    # For Linux it might be more correct to use ${MAKE_INSTALL_LOCALEDIR}, but that
    # uses share/locale_name/software_name layout instead of share/software_name/locale_files.
    DESTINATION ${IAITO_INSTALL_DATADIR}/translations
)

