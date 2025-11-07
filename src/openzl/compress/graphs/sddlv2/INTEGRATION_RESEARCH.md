# OpenZL Integration Research Notes

## Research Summary: Function Graph Pattern

Based on `zl_graph_api.h` and `test_functionGraphs.cpp`.

---

## ✅ Key Learnings

### 1. Function Graph Signature

```c
ZL_Report ZL_SDDLv2_compress(
    ZL_Graph* gctx,     // Graph context (for params, scratch space)
    ZL_Edge* inputs[],  // Array of input edges
    size_t nbInputs     // Number of inputs (must match descriptor)
) noexcept
```

### 2. Accessing Input Data

```c
// Get input data from edge
const ZL_Input* input = ZL_Edge_getData(inputs[0]);

// From ZL_Input, get raw bytes (need to look at zl_input.h)
// Likely: ZL_Input_getData(input) → const void*
//         ZL_Input_getSize(input) → size_t
```

### 3. Setting Output Destinations

**Simple case (1 output → 1 destination):**
```c
ZL_RET_R_IF_ERR(ZL_Edge_setDestination(edge, ZL_GRAPH_ZSTD));
```

**Multiple outputs:**
```c
for (size_t i = 0; i < outputs.nbEdges; i++) {
    ZL_RET_R_IF_ERR(
        ZL_Edge_setDestination(outputs.edges[i], ZL_GRAPH_ZSTD));
}
```

### 4. Running Nodes (Transform Data)

```c
// Run a transform node on an edge
ZL_TRY_LET_T(ZL_EdgeList, outputs, 
             ZL_Edge_runNode(input, ZL_NODE_SOME_TRANSFORM));

// outputs.nbEdges = number of output edges
// outputs.edges[i] = individual edges
```

### 5. Error Handling

```c
// Return on error
ZL_RET_R_IF_ERR(expression);

// Try and bind result (like Rust's let)
ZL_TRY_LET_T(Type, variable, expression);

// Success
return ZL_returnSuccess();
```

### 6. Passing Bytecode via Copy Parameters

**Approach:** Use `ZL_CopyParam` to pass bytecode safely.

```c
// Define bytecode parameter ID
#define ZL_SDDL2_BYTECODE_PID 5000

// Bytecode storage
uint8_t bytecode[] = { /* ... */ };
size_t bytecode_size = sizeof(bytecode);

// Setup copy parameter for bytecode
ZL_CopyParam bytecodeParam = {
    .paramId = ZL_SDDL2_BYTECODE_PID,
    .paramPtr = bytecode,
    .paramSize = bytecode_size
};

ZL_LocalCopyParams lcp = { &bytecodeParam, 1 };
ZL_LocalParams lParams = { .copyParams = lcp };

// Registration
static ZL_Type serialInputType = ZL_Type_serial;

static ZL_FunctionGraphDesc const sddlv2_desc = {
    .name = "SDDL2 Segment Generator",
    .graph_f = ZL_SDDLv2_compress,
    .inputTypeMasks = &serialInputType,
    .nbInputs = 1,
    .lastInputIsVariable = false,
    .localParams = lParams,  // ← Pass bytecode here!
};

ZL_GraphID gid = ZL_Compressor_registerFunctionGraph(
    compressor, &sddlv2_desc);
```

### 7. Accessing Bytecode in Graph Function

```c
ZL_RefParam bytecodeParam = ZL_Graph_getLocalRefParam(
    gctx, ZL_SDDL2_BYTECODE_PID);

const uint8_t* bytecode = (const uint8_t*)bytecodeParam.paramPtr;
size_t bytecode_size = bytecodeParam.paramSize;
```

**Why Copy Parameters:**
- ✅ OpenZL manages lifetime
- ✅ Thread-safe copying
- ✅ Simple and explicit
- 🔮 Opaque pointer can be explored later as optimization

### 8. Scratch Space Allocation

```c
void* buffer = ZL_Graph_getScratchSpace(gctx, size);
// Automatically freed at end of graph execution
```

---

## ✅ Key Insight: Split Node Pattern

### **You DON'T create ZL_Input objects from segment data!**

Instead, use the **SplitN Node** to divide the input Edge:

```c
// From zl_split.h:
ZL_RESULT_OF(ZL_EdgeList)
ZL_Edge_runSplitNode(
    ZL_Edge* input,
    const size_t* segmentSizes,  // Array of segment sizes
    size_t nbSegments            // Number of segments
);
```

**Pattern:**
1. SDDL2 receives single Input Edge (Serial/Byte[])
2. Run interpreter → get segment list (with **sizes only**)
3. Use `ZL_Edge_runSplitNode()` to split input by segment sizes
4. Split Node produces multiple output Edges (one per segment)
5. Set destination for each output Edge

**Example:**
```c
// Extract segment sizes from SDDL2_segment_list
size_t* segmentSizes = malloc(segments.count * sizeof(size_t));
for (size_t i = 0; i < segments.count; i++) {
    segmentSizes[i] = segments.items[i].size_bytes;
}

// Split the input edge
ZL_TRY_LET_T(ZL_EdgeList, outputs,
    ZL_Edge_runSplitNode(input, segmentSizes, segments.count));

// Set destination for each output edge
for (size_t i = 0; i < outputs.nbEdges; i++) {
    ZL_RET_R_IF_ERR(
        ZL_Edge_setDestination(outputs.edges[i], ZL_GRAPH_ZSTD));
}
```

**Key Properties:**
- If last segment size is `0`, it means "whatever is left"
- Each output Edge gets a channel ID (metadata: `ZL_SPLIT_CHANNEL_ID`)
- Split Node handles all memory management

---

## 📋 Implementation Plan for SDDL2

### Step 1: Minimal Skeleton ✅ NEXT
```c
ZL_Report ZL_SDDLv2_compress(
    ZL_Graph* gctx,
    ZL_Edge* inputs[],
    size_t nbInputs) noexcept
{
    assert(nbInputs == 1);
    // Just pass through to ZSTD for now
    return ZL_Edge_setDestination(inputs[0], ZL_GRAPH_ZSTD);
}
```

### Step 2: Extract Bytecode
```c
const uint8_t* bytecode = (const uint8_t*)ZL_Graph_getOpaquePtr(gctx);
// Assume bytecode is stored with size prefix or known length
```

### Step 3: Extract Input Data
```c
const ZL_Input* input_obj = ZL_Edge_getData(inputs[0]);
const void* input_data = ZL_Input_getData(input_obj);
size_t input_size = ZL_Input_getSize(input_obj);
```

### Step 4: Run Interpreter
```c
SDDL2_segment_list segments;
SDDL2_segment_list_init(&segments);

SDDL2_error err = SDDL2_execute_bytecode(
    bytecode, bytecode_size,
    input_data, input_size,
    &segments);

if (err != SDDL2_OK) {
    return ZL_returnError(...);
}
```

### Step 5: Split Input by Segment Sizes
```c
// Extract segment sizes
size_t* segmentSizes = ZL_Graph_getScratchSpace(
    gctx, segments.count * sizeof(size_t));

for (size_t i = 0; i < segments.count; i++) {
    segmentSizes[i] = segments.items[i].size_bytes;
}

// Split the input edge by segment sizes
ZL_TRY_LET_T(ZL_EdgeList, outputs,
    ZL_Edge_runSplitNode(inputs[0], segmentSizes, segments.count));

// Verify we got the expected number of outputs
assert(outputs.nbEdges == segments.count);
```

### Step 6: Set Destinations & Cleanup
```c
SDDL2_segment_list_destroy(&segments);
return ZL_returnSuccess();
```

---

## 📚 Reference Examples

### Simple Passthrough
```c
// test_functionGraphs.cpp:48-54
static ZL_Report justGoToZstd(
    ZL_Graph*, ZL_Edge* inputs[], size_t nbInputs) noexcept
{
    assert(nbInputs == 1);
    ZL_RET_R_IF_ERR(ZL_Edge_setDestination(inputs[0], ZL_GRAPH_ZSTD));
    return ZL_returnSuccess();
}
```

### Process and Split
```c
// test_functionGraphs.cpp:590-612
static ZL_Report dynGraphTree(
    ZL_Graph* gctx, ZL_Edge* inputs[], size_t nbIns) noexcept
{
    assert(nbIns == 1);
    ZL_Edge* input = inputs[0];
    
    // Convert serial to token4
    ZL_TRY_LET_T(ZL_EdgeList, sl1,
        ZL_Edge_runNode(input, ZL_NODE_CONVERT_SERIAL_TO_TOKEN4));
    
    // Transpose and split into 4 edges
    ZL_TRY_LET_T(ZL_EdgeList, sl2,
        ZL_Edge_runNode(sl1.edges[0], ZL_NODE_TRANSPOSE_SPLIT));
    
    // Set destination for each edge
    for (size_t i = 0; i < sl2.nbEdges; i++) {
        ZL_RET_R_IF_ERR(
            ZL_Edge_setDestination(sl2.edges[i], ZL_GRAPH_ZSTD));
    }
    
    return ZL_returnSuccess();
}
```

---

## Next Actions

1. ✅ Complete this research document
2. ⏭️ Search for `ZL_Input` creation APIs
3. ⏭️ Implement minimal skeleton
4. ⏭️ Test with simple bytecode

