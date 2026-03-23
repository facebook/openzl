enable_testing()

# Python integration tests
find_program(PYTHON_EXECUTABLE python3 REQUIRED)

function(add_cli_integration_test TEST_NAME TEST_METHOD)
    add_test(
        NAME ${TEST_NAME}
        COMMAND ${PYTHON_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/cli_integration_tests.py
            $<TARGET_FILE:zli>
            ${TEST_METHOD}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
endfunction()

add_cli_integration_test(serial_test
    SerialTest.test_compress_decompress)
add_cli_integration_test(csv_default_test
    CsvTest.test_train_compress_decompress)
add_cli_integration_test(csv_greedy_test
    CsvGreedyTest.test_train_compress_decompress)
add_cli_integration_test(csv_full_split_test
    CsvFullSplitTest.test_train_compress_decompress)
add_cli_integration_test(csv_bottom_up_test
    CsvBottomUpTest.test_train_compress_decompress)
add_cli_integration_test(csv_chunked_test
    CsvChunkedTest.test_train_compress_decompress)
add_cli_integration_test(csv_train_inline_test
    CsvTrainInlineTest.test_train_inline)
add_cli_integration_test(ace_train_inline_test
    AceTrainInlineTest.test_train_inline)
add_cli_integration_test(parquet_test
    ParquetTest.test_train_compress_decompress)
add_cli_integration_test(csv_alternative_separator_test
    CsvAlternativeSeparatorTest.test_compress_decompress)
add_cli_integration_test(benchmark_csv_compression_test
    BenchmarkCsvCompressionTest.test_benchmark)
add_cli_integration_test(strict_mode_permissive_test
    StrictModeTest.test_permissive_mode_succeeds)
add_cli_integration_test(strict_mode_fails_test
    StrictModeTest.test_strict_mode_fails)
add_cli_integration_test(u8_test
    U8Test.test_compress_decompress)
add_cli_integration_test(u8_train_test
    U8TrainTest.test_train_compress_decompress)
add_cli_integration_test(ml_selector_test
    MLSelectorTest.test_train_compress_decompress)
add_cli_integration_test(ml_dynamic_successor_test
    MLDynamicSuccessorTest.test_dynamic_ml_successors)
add_cli_integration_test(
    ml_dynamic_successor_compression_ratio_test
    MLDynamicSuccessorTest.test_compression_ratios)
add_cli_integration_test(sddl2_test
    SDDL2TrainTest.test_train_compress_decompress)
