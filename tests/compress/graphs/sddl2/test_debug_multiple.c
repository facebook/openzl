#include <stdio.h>
#include <stdint.h>
#include "generated_test_bytecode.h"
#include "openzl/compress/graphs/sddl2/sddl2_interpreter.h"

int main(void) {
    uint8_t input[] = "0123456789ABC";
    
    SDDL2_Segment_list segments;
    SDDL2_Segment_list_init(&segments, NULL, NULL);
    
    SDDL2_Error err = SDDL2_execute_bytecode(
            BYTECODE_TEST_PUSH_CURRENT_POS_MULTIPLE,
            BYTECODE_TEST_PUSH_CURRENT_POS_MULTIPLE_SIZE,
            input,
            sizeof(input) - 1,
            &segments);
    
    printf("Error: %d\n", err);
    printf("Segment count: %zu\n", segments.count);
    for (size_t i = 0; i < segments.count; i++) {
        printf("  Segment %zu: tag=%u, start=%zu, size=%zu\n", 
               i, segments.items[i].tag, segments.items[i].start_pos, segments.items[i].size_bytes);
    }
    
    SDDL2_Segment_list_destroy(&segments);
    return 0;
}
