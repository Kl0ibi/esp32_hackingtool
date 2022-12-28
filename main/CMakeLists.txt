idf_component_register(SRCS "hackingtool.c"
                    )
target_link_libraries(${COMPONENT_LIB} -Wl,-zmuldefs)



