// Copyright (c) Meta Platforms, Inc. and affiliates.

import {decode} from 'cbor2';
import type {SerializedStreamdumpV0, SerializedStreamdumpV1} from '../interfaces/SerializedStreamdump';
import {Streamdump} from '../models/Streamdump';
import type {StreamID, ZL_IDType} from '../models/idTypes';
import type {SerializedChunk} from '../interfaces/SerializedChunk';

// format version 0, need to convert to format version 1
function convertV0toV1(obj: SerializedStreamdumpV0): SerializedStreamdumpV1 {
  const chunk: SerializedChunk = {
    chunkId: 0,
    streams: obj.streams,
    codecs: obj.codecs,
    graphs: obj.graphs,
  };

  // insert a root node before the first node
  chunk.codecs.unshift({
    name: 'zl.#start',
    cType: true,
    cID: 0 as ZL_IDType,
    cHeaderSize: 0,
    cFailureString: '',
    cLocalParams: {
      copyParams: [],
      intParams: [],
      refParams: [],
    },
    inputStreams: [],
    outputStreams: [0 as StreamID],
    chunkId: 0,
  });

  // rename all stream source/targets
  chunk.streams.forEach((stream) => {
    stream.outputIdx += 1;
  });
  chunk.graphs.forEach((graph) => {
    for (let i = 0; i < graph.codecIDs.length; ++i) {
      graph.codecIDs[i] += 1;
    }
  });

  const retval: SerializedStreamdumpV1 = {
    frameVersion: -1,
    libraryVersion: 100,
    traceVersion: 0,
    chunks: [chunk],
  };
  return retval;
}

function marshallV0(obj: object): SerializedStreamdumpV1 {
  if (
    !(
      'streams' in obj &&
      Array.isArray(obj.streams) &&
      'codecs' in obj &&
      Array.isArray(obj.codecs) &&
      'graphs' in obj &&
      Array.isArray(obj.graphs)
    )
  ) {
    throw new Error(
      'Decoded object does not have an explicit format version string but does not fit the structure of V0',
    );
  }
  return convertV0toV1(obj as SerializedStreamdumpV0);
}

function marshallV1(obj: object): SerializedStreamdumpV1 {
  if (
    !(
      typeof obj === 'object' &&
      obj !== null &&
      'chunks' in obj &&
      Array.isArray(obj.chunks) &&
      obj.chunks.length !== 0 &&
      'libraryVersion' in obj &&
      typeof obj.libraryVersion === 'number' &&
      'frameVersion' in obj &&
      typeof obj.frameVersion === 'number'
    )
  ) {
    throw new Error('Decoded object is declared to be V1 but is malformed.');
  }

  return {
    libraryVersion: obj.libraryVersion,
    frameVersion: obj.frameVersion,
    traceVersion: 1,
    chunks: obj.chunks,
  };
}

/**
 * Convert presented object into a valid SerializedStreamdump object with the latest format version.
 * @throw error if no conversion is possible.
 */
function marshall(obj: unknown): SerializedStreamdumpV1 {
  if (typeof obj !== 'object' || obj === null) {
    throw new Error('Decoded data is not a valid object');
  }
  // determine format version
  if (!('traceVersion' in obj && typeof obj.traceVersion === 'number')) {
    return marshallV0(obj as object);
  }
  return marshallV1(obj as object);
}

export async function extractStreamdumpFromCborFile(file: File): Promise<Streamdump> {
  // load the data
  try {
    const buffer = await file.arrayBuffer();
    const decodedCborData = decode(new Uint8Array(buffer));
    // Validate wire format and massage old formats into the latest
    return Streamdump.fromObject(marshall(decodedCborData));
  } catch (error) {
    console.error('Error decoding CBOR file', error);
    throw error;
  }
}
