// Copyright (c) Meta Platforms, Inc. and affiliates.

import React from 'react';

/*
 * File useful for making/adding SVG icons to use in the application. This is preferred over importing entire React image
 * libraries from online as those libraries are very heavy-weight, and exceed the byte limit for what shuold be pushed to
 * our code base.
 */

interface IconProps {
  className?: string;
  style?: React.CSSProperties;
  size?: number | string;
}

export function TbLayoutBottombarCollapseFilled({className, style, size = 24}: IconProps) {
  return (
    <svg width={size} height={size} viewBox="0 0 24 24" fill="currentColor" className={className} style={style}>
      <path d="M18 3a3 3 0 0 1 2.995 2.824l.005 .176v12a3 3 0 0 1 -2.824 2.995l-.176 .005h-12a3 3 0 0 1 -2.995 -2.824l-.005 -.176v-12a3 3 0 0 1 2.824 -2.995l.176 -.005h12zm0 2h-12a1 1 0 0 0 -.993 .883l-.007 .117v9h14v-9a1 1 0 0 0 -.883 -.993l-.117 -.007zm-7.387 3.21l.094 .083l1.293 1.292l1.293 -1.292a1 1 0 0 1 1.32 -.083l.094 .083a1 1 0 0 1 .083 1.32l-.083 .094l-2 2a1 1 0 0 1 -1.32 .083l-.094 -.083l-2 -2a1 1 0 0 1 1.32 -1.497z" />
    </svg>
  );
}

export function TbLayoutBottombarExpandFilled({className, style, size = 24}: IconProps) {
  return (
    <svg width={size} height={size} viewBox="0 0 24 24" fill="currentColor" className={className} style={style}>
      <path d="M18 3a3 3 0 0 1 2.995 2.824l.005 .176v12a3 3 0 0 1 -2.824 2.995l-.176 .005h-12a3 3 0 0 1 -2.995 -2.824l-.005 -.176v-12a3 3 0 0 1 2.824 -2.995l.176 -.005h12zm0 2h-12a1 1 0 0 0 -.993 .883l-.007 .117v9h14v-9a1 1 0 0 0 -.883 -.993l-.117 -.007zm-5.387 3.21l.094 .083l2 2a1 1 0 0 1 -1.32 1.497l-.094 -.083l-1.293 -1.292l-1.293 1.292a1 1 0 0 1 -1.32 .083l-.094 -.083a1 1 0 0 1 -.083 -1.32l.083 -.094l2 -2a1 1 0 0 1 1.32 -.083z" />
    </svg>
  );
}

export function TbSquareRoundedChevronDownFilled({className, style, size = 24}: IconProps) {
  return (
    <svg width={size} height={size} viewBox="0 0 24 24" fill="currentColor" className={className} style={style}>
      <path d="M12 2c-.218 0 -.432 .002 -.642 .005l-.616 .017l-.299 .013l-.579 .034l-.553 .046c-4.785 .464 -6.732 2.411 -7.196 7.196l-.046 .553l-.034 .579c-.005 .098 -.01 .198 -.013 .299l-.017 .616l-.004 .318l-.001 .324c0 .218 .002 .432 .005 .642l.017 .616l.013 .299l.034 .579l.046 .553c.464 4.785 2.411 6.732 7.196 7.196l.553 .046l.579 .034c.098 .005 .198 .01 .299 .013l.616 .017l.642 .005l.642 -.005l.616 -.017l.299 -.013l.579 -.034l.553 -.046c4.785 -.464 6.732 -2.411 7.196 -7.196l.046 -.553l.034 -.579c.005 -.098 .01 -.198 .013 -.299l.017 -.616l.005 -.642l-.005 -.642l-.017 -.616l-.013 -.299l-.034 -.579l-.046 -.553c-.464 -4.785 -2.411 -6.732 -7.196 -7.196l-.553 -.046l-.579 -.034a28.058 28.058 0 0 0 -.299 -.013l-.616 -.017l-.318 -.004l-.324 -.001zm-3.707 8.293a1 1 0 0 1 1.32 -.083l.094 .083l2.293 2.292l2.293 -2.292a1 1 0 0 1 1.32 -.083l.094 .083a1 1 0 0 1 .083 1.32l-.083 .094l-3 3a1 1 0 0 1 -1.32 .083l-.094 -.083l-3 -3a1 1 0 0 1 1.32 -1.497z" />
    </svg>
  );
}

export function TbArrowsMinimize({className, style, size = 24}: IconProps) {
  return (
    <svg
      width={size}
      height={size}
      viewBox="0 0 24 24"
      fill="none"
      stroke="currentColor"
      strokeWidth="2"
      strokeLinecap="round"
      strokeLinejoin="round"
      className={className}
      style={style}>
      <path d="M5 9l4 0l0 -4" />
      <path d="M3 3l6 6" />
      <path d="M5 15l4 0l0 4" />
      <path d="M3 21l6 -6" />
      <path d="M19 9l-4 0l0 -4" />
      <path d="M15 9l6 -6" />
      <path d="M19 15l-4 0l0 4" />
      <path d="M15 15l6 6" />
    </svg>
  );
}

export function TbArrowsMaximize({className, style, size = 24}: IconProps) {
  return (
    <svg
      width={size}
      height={size}
      viewBox="0 0 24 24"
      fill="none"
      stroke="currentColor"
      strokeWidth="2"
      strokeLinecap="round"
      strokeLinejoin="round"
      className={className}
      style={style}>
      <path d="M16 4l4 0l0 4" />
      <path d="M14 10l6 -6" />
      <path d="M8 20l-4 0l0 -4" />
      <path d="M4 20l6 -6" />
      <path d="M16 20l4 0l0 -4" />
      <path d="M14 14l6 6" />
      <path d="M8 4l-4 0l0 4" />
      <path d="M4 4l6 6" />
    </svg>
  );
}

export function CiCircleMore({className, style, size = 24}: IconProps) {
  return (
    <svg
      width={size}
      height={size}
      viewBox="0 0 24 24"
      fill="currentColor"
      stroke="none"
      className={className}
      style={style}>
      <g>
        <circle cx="12.001" cy="12" r="1" />
        <circle cx="16.001" cy="12" r="1" />
        <circle cx="8.001" cy="12" r="1" />
        <path d="M12,21.932A9.934,9.934,0,1,1,21.934,12,9.944,9.944,0,0,1,12,21.932ZM12,3.065A8.934,8.934,0,1,0,20.934,12,8.944,8.944,0,0,0,12,3.065Z" />
      </g>
    </svg>
  );
}
