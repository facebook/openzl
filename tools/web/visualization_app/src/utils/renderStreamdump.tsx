// Copyright (c) Meta Platforms, Inc. and affiliates.

import React from 'react';
import {Streamdump} from '../models/Streamdump';
import {Stream} from '../models/Stream';
import {Codec} from '../models/Codec';
import {Graph} from '../models/Graph';

function renderStreams(streams: Stream[]): React.ReactNode {
  return streams.map((stream, streamId) => <div key={`stream-${streamId}`}>{stream.toStringDotFormat()}</div>);
}

function renderCodecsAndGraphs(codecs: Codec[], graphs: Graph[]): React.ReactNode {
  const elements: React.ReactNode[] = [];
  let graphIdx = 0; // index of the current graph being rendered
  // logic to output codecs and their associated graphs in DOT format
  for (let codecNum = 0; codecNum < codecs.length; codecNum++) {
    const currGraphCodecIDs = graphs[graphIdx].codecIDs;
    if (graphIdx < graphs.length && codecNum === currGraphCodecIDs[0]) {
      // graph starting
      elements.push(
        <div key={`graph-start-${graphIdx}`} style={{whiteSpace: 'pre-wrap'}}>
          {graphs[graphIdx].toStringDotFormatStart()}
        </div>,
      );
    }
    // codec
    elements.push(<div key={`codec-${codecNum}`}>{codecs[codecNum].toStringDotFormat()}</div>);
    // graph end
    if (graphIdx < graphs.length && codecNum === currGraphCodecIDs[currGraphCodecIDs.length - 1]) {
      elements.push(<div key={`graph-end-${graphIdx}`}>{graphs[graphIdx].toStringDotFormatEnd()}</div>);
      graphIdx++;
    }
  }
  return elements;
}

export function renderStreamdump(streamdump: Streamdump): React.ReactNode {
  return (
    <div>
      digraph stream_topo {'{'}
      {renderStreams(streamdump.chunks[0].streams)}
      <br />
      {renderCodecsAndGraphs(streamdump.chunks[0].codecs, streamdump.chunks[0].graphs)}
      {'}'}
    </div>
  );
}
