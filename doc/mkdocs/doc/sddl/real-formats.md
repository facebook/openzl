# Working with Real Formats

*Chapter 8 - Practical examples of describing real-world file formats*

---

## Introduction

### The Gap Between Specification and Implementation

### Common Format Categories

### Goals of This Chapter

---

## Delimiter-Based Parsing

### Basic Delimiter Syntax

#### String Delimiters

#### Binary Byte Sequence Delimiters

#### Multiple-Byte Delimiters

### The `include_delim` Option

### Error Handling for Missing Delimiters

### Performance Implications

### When to Use Delimiter-Based Parsing

---

## Text-Based Protocols

### Line-Based Formats

#### Unix Text Files (LF)

#### Windows Text Files (CRLF)

#### Mixed Line Endings

### HTTP Headers Example

#### Request Line

#### Header Fields

#### Body Separation

#### Complete HTTP Request

### SMTP Protocol Example

### CSV and TSV Formats

#### Basic CSV Structure

#### Quoted Fields

#### Escape Sequences

#### Header Rows

### INI Configuration Files

### JSON-like Text Formats

---

## Mixed Binary and Text Formats

### Common Patterns

#### Binary Header + Text Body

#### Text Header + Binary Body

#### Interleaved Text and Binary

### Email Messages (MIME)

#### Headers (Text)

#### Body (Binary or Text)

#### Multipart Messages

#### Attachments

### Network Protocols

#### Text Commands + Binary Payloads

#### Length-Prefixed Binary Data

---

## Image Formats

### BMP (Bitmap) Format

#### File Header

#### DIB Header

#### Pixel Data

#### Padding Considerations

### PNG Format

#### File Signature

#### Chunk Structure

#### IHDR Chunk

#### IDAT Chunks

#### IEND Chunk

#### CRC Validation

### TIFF Format Basics

---

## Archive and Container Formats

### ZIP File Format

#### Local File Headers

#### File Data

#### Central Directory

#### End of Central Directory

### TAR Archives

#### Header Block (512 bytes)

#### File Content

#### Padding to 512-byte Boundaries

### Custom Container Formats

---

## Executable and Object Files

### ELF (Executable and Linkable Format)

#### ELF Header

#### Program Headers

#### Section Headers

#### String Tables

### Mach-O Format (macOS)

### PE Format (Windows) Basics

---

## Database and Storage Formats

### SQLite Database File

#### Header

#### Page Structure

#### B-tree Pages

### HDF5 Format

#### Superblock

#### B-tree Nodes

#### Data Objects

---

## Media Formats

### WAVE Audio Format

#### RIFF Header

#### Format Chunk

#### Data Chunk

#### Additional Chunks

### MP3 ID3 Tags

#### ID3v1

#### ID3v2

### MP4/MOV Container

#### Atoms/Boxes Structure

#### ftyp Box

#### moov Box

#### mdat Box

---

## Network Packet Formats

### Ethernet Frame

### IP Packet Header

### TCP Segment

### UDP Datagram

### Custom Protocol Example

---

## Document Formats

### PDF Structure Basics

#### Header

#### Body (Objects)

#### Cross-Reference Table

#### Trailer

### Office Open XML (DOCX) Structure

---

## Dealing with Complexity

### Modular Decomposition

### Separating Header from Payload

### Handling Multiple Versions

### Optional and Extension Fields

### Validation Strategies

---

## Character Encoding Considerations

### ASCII vs UTF-8

### Encoding Detection

### Null-Terminated Strings

### Length-Prefixed Strings

### Fixed-Length Strings

---

## Endianness in Practice

### Network Byte Order (Big-Endian)

### Little-Endian Dominance

### Mixed-Endian Formats

### Byte Order Marks (BOM)

---

## Debugging Format Descriptions

### Common Errors and Solutions

#### Incorrect Size Calculations

#### Misaligned Fields

#### Wrong Endianness

#### Missing Delimiters

#### Validation Failures

### Testing Strategies

#### Minimal Valid File

#### Maximal Valid File

#### Boundary Conditions

#### Invalid Files

#### Corrupted Files

### Hex Dump Analysis

### Comparing Against Reference Implementations

---

## Step-by-Step Walkthrough

### Example: Describing a Simple Custom Format

#### Requirements

#### Iterative Development

#### Testing Each Stage

#### Final Implementation

---

## Performance Considerations

### When Delimiter Parsing Is Acceptable

### Optimizing for Common Cases

### Instant-Parse Opportunities

### Hybrid Approaches

---

## Format Evolution in the Wild

### Versioning Strategies

### Backward Compatibility

### Forward Compatibility

### Migration Paths

---

## Common Patterns and Idioms

### Magic Numbers and Signatures

### Length-Prefixed Data

### Chunk-Based Structures

### Tagged Data

### Null Termination

### Padding and Alignment

---

## Tools and Techniques

### Hex Editors

### Binary Diff Tools

### Protocol Analyzers

### Format Validators

### Reference Implementations

---

## Case Studies

### Case Study 1: Adding SDDL to an Existing Format

### Case Study 2: Modernizing a Legacy Format

### Case Study 3: Creating a New Format from Scratch

---

## Summary

### Key Lessons

### Format Description Checklist

### Resources for Format Specifications

---

## Further Reading
