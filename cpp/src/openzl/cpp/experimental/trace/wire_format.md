# Trace CBOR Wire Format
!!! WARNING
    The format is evolving and the following description is liable to be out of date if not maintained.
    This is provided as a convenience, not a prescription.

## Version 1 (dev)
The wire format of the CBOR file is as follows:
```
Trace = {
    libraryVersion: Int32LE; # ZL_LIBRARY_VERSION_NUMBER from zl_version.h
    frameVersion: Int32LE;   # The frame version used for the trace
    traceVersion: Int32LE;   # A specific version number for the trace CBOR
    streams: StreamVisualizer[];
    codecs: Codec[];
    graphs: Graph[];
}

StreamVisualizer = {
    type: Int64LE; # ZL_Type
    outputIdx: Int64LE;
    eltWidth: Int64LE;
    numElts: Int64LE;
    cSize: Int64LE;
    share: Float64LE;
    contentSize: Int64LE;
}

Codec = {
    name: String;
    cType: Boolean; # Standard vs Custom
    cID: Int64LE;
    cHeaderSize: Int64LE;
    cFailureString: String | null;
    cLocalParams: LocalParamInfo;
    inputStreams: Int64LE[];
    outputStreams: Int64LE[];
}

Graph = {
    gType: Int64LE; # ZL_GraphType
    gName: String;
    gFailureString: String | null;
    gLocalParams: LocalParamInfo;
    codecIDs: Int64LE[];
}

LocalParamInfo = {
    intParams: IntParamInfo[];
    copyParams: CopyParamInfo[];
    refParams: RefParamInfo[];
}

IntParamInfo = {
    paramId: Int64LE;
    paramValue: Int64LE;
}

CopyParamInfo = {
    paramId: Int64LE;
    paramSize: Int64LE;
    paramData: Bytes | null;
}

RefParamInfo = {
    paramId: Int64LE;
}
```

## Unversioned (v0.1.0)