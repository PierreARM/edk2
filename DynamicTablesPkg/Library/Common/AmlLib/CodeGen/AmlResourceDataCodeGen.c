/** @file
  AML Resource Data Code Generation.

  Copyright (c) 2020 - 2021, Arm Limited. All rights reserved.<BR>

  SPDX-License-Identifier: BSD-2-Clause-Patent

  @par Glossary:
  - Rd or RD   - Resource Data
  - Rds or RDS - Resource Data Small
  - Rdl or RDL - Resource Data Large
**/

#include <AmlNodeDefines.h>
#include <CodeGen/AmlResourceDataCodeGen.h>

#include <AmlCoreInterface.h>
#include <AmlDefines.h>
#include <Api/AmlApiHelper.h>
#include <Tree/AmlNode.h>
#include <ResourceData/AmlResourceData.h>

/** If ParentNode is not NULL, append RdNode.
    If NewRdNode is not NULL, update its value to RdNode.

  @param [in]  RdNode       Newly created Resource Data node.
                            RdNode is deleted if an error occurs.
  @param [in]  ParentNode   If not NULL, ParentNode must:
                             - be a NameOp node, i.e. have the AML_NAME_OP
                               opcode (cf "Name ()" ASL statement)
                             - contain a list of resource data elements
                               (cf "ResourceTemplate ()" ASL statement)
                            RdNode is then added at the end of the variable
                            list of resource data elements, but before the
                            "End Tag" Resource Data.
  @param [out] NewRdNode    If not NULL, update the its value to RdNode.

  @retval  EFI_SUCCESS            The function completed successfully.
  @retval  EFI_INVALID_PARAMETER  Invalid parameter.
**/
STATIC
EFI_STATUS
EFIAPI
LinkRdNode (
  IN  AML_DATA_NODE      * RdNode,
  IN  AML_OBJECT_NODE    * ParentNode,
  OUT AML_DATA_NODE     ** NewRdNode
  )
{
  EFI_STATUS        Status;
  EFI_STATUS        Status1;
  AML_OBJECT_NODE   *BufferOpNode;

  if (NewRdNode != NULL) {
    *NewRdNode = RdNode;
  }

  if (ParentNode != NULL) {
    // Check this is a NameOp node.
    if ((!AmlNodeHasOpCode (ParentNode, AML_NAME_OP, 0))) {
      ASSERT (0);
      Status = EFI_INVALID_PARAMETER;
      goto error_handler;
    }

    // Get the value which is represented as a BufferOp object node
    // which is the 2nd fixed argument (i.e. index 1).
    BufferOpNode = (AML_OBJECT_NODE_HANDLE)AmlGetFixedArgument (
                                             ParentNode,
                                             EAmlParseIndexTerm1
                                             );
    if ((BufferOpNode == NULL)                                             ||
        (AmlGetNodeType ((AML_NODE_HANDLE)BufferOpNode) != EAmlNodeObject) ||
        (!AmlNodeHasOpCode (BufferOpNode, AML_BUFFER_OP, 0))) {
      ASSERT (0);
      Status = EFI_INVALID_PARAMETER;
      goto error_handler;
    }

    // Add RdNode as the last element, but before the EndTag.
    Status = AmlAppendRdNode (BufferOpNode, RdNode);
    if (EFI_ERROR (Status)) {
      ASSERT (0);
      goto error_handler;
    }
  }

  return Status;

error_handler:
  Status1 = AmlDeleteTree ((AML_NODE_HEADER*)RdNode);
  ASSERT_EFI_ERROR (Status1);
  // Return original error.
  return Status;
}

/** Populate the TypeSpecificFlags field for IO ranges.

  @param [in]  IsaRanges            Available values are:
                                     0-Reserved
                                     1-NonISAOnly
                                     2-ISAOnly
                                     3-EntireRange
                                    See ACPI 6.4 spec, s19.6.34 for more.
  @param [in]  IsDenseTranslation   TranslationDensity parameter,
                                    See ACPI 6.4 spec, s19.6.34 for more.
  @param [in]  IsTypeStatic         TranslationType parameter,
                                    See ACPI 6.4 spec, s19.6.34 for more.

  @return A TypeSpecificFlagsvalue.
          MAX_UINT8 if error.
**/
STATIC
UINT8
EFIAPI
RdIoRangeSpecificFlags (
  IN  UINT8     IsaRanges,
  IN  BOOLEAN   IsDenseTranslation,
  IN  BOOLEAN   IsTypeStatic
  )
{
  // Only check type specific parameters.
  if (IsaRanges > 3) {
    ASSERT (0);
    return MAX_UINT8;
  }

  // Populate TypeSpecificFlags and call the generic function.
  // Cf ACPI 6.4 specification, Table 6.50:
  // "Table 6.50: I/O Resource Flag (Resource Type = 1) Definitions"
  return  IsaRanges                 |
          (IsTypeStatic ? 0 : BIT4) |
          (IsDenseTranslation ? 0 : BIT5);
}

/** Populate the TypeSpecificFlags field for Memory ranges.

  @param [in]  Cacheable            Available values are:
                                    0-The memory is non-cacheable
                                    1-The memory is cacheable
                                    2-The memory is cacheable and supports
                                      write combining
                                    3-The memory is cacheable and prefetchable
  @param [in]  IsReadWrite          ReadAndWrite parameter,
                                    See ACPI 6.4 spec, s19.6.35 for more.
  @param [in]  MemoryRangeType      Available values are:
                                      0-AddressRangeMemory
                                      1-AddressRangeReserved
                                      2-AddressRangeACPI
                                      3-AddressRangeNVS
                                    See ACPI 6.4 spec, s19.6.35 for more.
  @param [in]  IsTypeStatic         TranslationType parameter,
                                    See ACPI 6.4 spec, s19.6.35 for more.

  @return A TypeSpecificFlagsvalue.
          MAX_UINT8 if error.
**/
STATIC
UINT8
EFIAPI
MemoryRangeSpecificFlags (
  IN  UINT8     Cacheable,
  IN  BOOLEAN   IsReadWrite,
  IN  UINT8     MemoryRangeType,
  IN  BOOLEAN   IsTypeStatic
  )
{
  // Only check type specific parameters.
  if ((Cacheable > 3) ||
      (MemoryRangeType > 3)) {
    ASSERT (0);
    return MAX_UINT8;
  }

  // Populate TypeSpecificFlags and call the generic function.
  // Cf ACPI 6.4 specification, Table 6.49:
  // "Memory Resource Flag (Resource Type = 0) Definitions"
  return (IsReadWrite ? BIT0 : 0)  |
         (Cacheable << 1)          |
         (MemoryRangeType << 3)    |
         (IsTypeStatic ? 0 : BIT5);
}

/** Populate the GeneralFlags field of any Address Space Resource Descriptors.

  E.g.:
  ACPI 6.4 specification, s6.4.3.5.1 "QWord Address Space Descriptor"
  for QWord

  @param [in]  IsPosDecode          Decode parameter,
                                    See ACPI 6.4 spec, s19.6.36 for more.
  @param [in]  IsMinFixed           See ACPI 6.4 spec, s19.6.36 for more.
  @param [in]  IsMaxFixed           See ACPI 6.4 spec, s19.6.36 for more.

  @return A TypeSpecificFlagsvalue.
**/
STATIC
UINT8
EFIAPI
AddressSpaceGeneralFlags (
  IN  BOOLEAN   IsPosDecode,
  IN  BOOLEAN   IsMinFixed,
  IN  BOOLEAN   IsMaxFixed
  )
{
  return (IsPosDecode ? 0 : BIT1)    |
         (IsMinFixed ? BIT2 : 0)     |
         (IsMaxFixed ? BIT3 : 0);
}

/** Check Address Space Descriptor Fields.

  Cf. ACPI 6.4 Table 6.44:
  "Valid Combination of Address Space Descriptor Fields"

  @param [in]  IsMinFixed           See ACPI 6.4 spec, s19.6.36 for more.
  @param [in]  IsMaxFixed           See ACPI 6.4 spec, s19.6.36 for more.
  @param [in]  AddressGranularity   See ACPI 6.4 spec, s19.6.36 for more.
  @param [in]  AddressMinimum       See ACPI 6.4 spec, s19.6.36 for more.
  @param [in]  AddressMaximum       See ACPI 6.4 spec, s19.6.36 for more.
  @param [in]  AddressTranslation   See ACPI 6.4 spec, s19.6.36 for more.
  @param [in]  RangeLength          See ACPI 6.4 spec, s19.6.36 for more.

  @retval EFI_SUCCESS             The function completed successfully.
  @retval EFI_INVALID_PARAMETER   Invalid parameter.
**/
STATIC
EFI_STATUS
EFIAPI
CheckAddressSpaceFields (
  IN  BOOLEAN   IsMinFixed,
  IN  BOOLEAN   IsMaxFixed,
  IN  UINT64    AddressGranularity,
  IN  UINT64    AddressMinimum,
  IN  UINT64    AddressMaximum,
  IN  UINT64    AddressTranslation,
  IN  UINT64    RangeLength
  )
{
  if ((AddressMinimum > AddressMaximum)                     ||
      (RangeLength > (AddressMaximum - AddressMinimum + 1)) ||
      ((AddressGranularity != 0) &&
       ((AddressGranularity + 1) & AddressGranularity) != 0)) {
    ASSERT (0);
    return EFI_INVALID_PARAMETER;
  }

  if (RangeLength != 0) {
    if (IsMinFixed ^ IsMaxFixed) {
      ASSERT (0);
      return EFI_INVALID_PARAMETER;
    } else if (IsMinFixed                 &&
               IsMaxFixed                 &&
               (AddressGranularity != 0)  &&
               ((AddressMaximum - AddressMinimum + 1) != RangeLength)) {
      ASSERT (0);
      return EFI_INVALID_PARAMETER;
    }
  } else {
    if (IsMinFixed && IsMaxFixed) {
      ASSERT (0);
      return EFI_INVALID_PARAMETER;
    } else if (IsMinFixed &&
              ((AddressMinimum & AddressGranularity) != 0)) {
      ASSERT (0);
      return EFI_INVALID_PARAMETER;
    } else if (IsMaxFixed &&
              (((AddressMaximum + 1) & AddressGranularity) != 0)) {
      ASSERT (0);
      return EFI_INVALID_PARAMETER;
    }
  }

  return EFI_SUCCESS;
}

/** Code generation for the "DWordSpace ()" ASL function.

  The Resource Data effectively created is an Extended Interrupt Resource
  Data. Cf ACPI 6.4:
   - s6.4.3.5.2 "DWord Address Space Descriptor".
   - s19.6.36 "DWordSpace".

  The created resource data node can be:
   - appended to the list of resource data elements of the NameOpNode.
     In such case NameOpNode must be defined by a the "Name ()" ASL statement
     and initially contain a "ResourceTemplate ()".
   - returned through the NewRdNode parameter.

  @param [in]  ResourceType         See ACPI 6.4 spec, s6.4.3.5.2 for more.
                                    Available values are:
                                      0:        Memory range
                                      1:        I/O range
                                      2:        Bus number range
                                      3-191:    Reserved
                                      192-255:  Hardware Vendor Defined
  @param [in]  IsResourceConsumer   ResourceUsage parameter,
                                    See ACPI 6.4 spec, s19.6.36 for more.
  @param [in]  IsPosDecode          Decode parameter,
                                    See ACPI 6.4 spec, s19.6.36 for more.
  @param [in]  IsMinFixed           See ACPI 6.4 spec, s19.6.36 for more.
  @param [in]  IsMaxFixed           See ACPI 6.4 spec, s19.6.36 for more.
  @param [in]  TypeSpecificFlags    See ACPI 6.4 spec, s6.4.3.5.5
                                    "Resource Type Specific Flags".
  @param [in]  AddressGranularity   See ACPI 6.4 spec, s19.6.36 for more.
  @param [in]  AddressMinimum       See ACPI 6.4 spec, s19.6.36 for more.
  @param [in]  AddressMaximum       See ACPI 6.4 spec, s19.6.36 for more.
  @param [in]  AddressTranslation   See ACPI 6.4 spec, s19.6.36 for more.
  @param [in]  RangeLength          See ACPI 6.4 spec, s19.6.36 for more.
  @param [in]  ResourceSourceIndex  See ACPI 6.4 spec, s19.6.36 for more.
                                    Not supported.
  @param [in]  ResourceSource       See ACPI 6.4 spec, s19.6.36 for more.
                                    Not supported.
  @param [in]  NameOpNode           NameOp object node defining a named object.
                                    If provided, append the new resource data
                                    node to the list of resource data elements
                                    of this node.
  @param [out] NewRdNode            If provided and success,
                                    contain the created node.

  @retval EFI_SUCCESS             The function completed successfully.
  @retval EFI_INVALID_PARAMETER   Invalid parameter.
  @retval EFI_OUT_OF_RESOURCES    Could not allocate memory.
**/
STATIC
EFI_STATUS
EFIAPI
AmlCodeGenRdDWordSpace (
  IN        UINT8                   ResourceType,
  IN        BOOLEAN                 IsResourceConsumer,
  IN        BOOLEAN                 IsPosDecode,
  IN        BOOLEAN                 IsMinFixed,
  IN        BOOLEAN                 IsMaxFixed,
  IN        UINT8                   TypeSpecificFlags,
  IN        UINT32                  AddressGranularity,
  IN        UINT32                  AddressMinimum,
  IN        UINT32                  AddressMaximum,
  IN        UINT32                  AddressTranslation,
  IN        UINT32                  RangeLength,
  IN        UINT8                   ResourceSourceIndex,
  IN  CONST CHAR8                   *ResourceSource,
  IN        AML_OBJECT_NODE_HANDLE  NameOpNode, OPTIONAL
  OUT       AML_DATA_NODE_HANDLE    *NewRdNode  OPTIONAL
  )
{
  EFI_STATUS                                Status;
  AML_DATA_NODE                           * RdNode;
  EFI_ACPI_DWORD_ADDRESS_SPACE_DESCRIPTOR   RdDWord;

  // ResourceSource and ResourceSourceIndex are not supported.
  if ((TypeSpecificFlags == MAX_UINT8)  ||
      (ResourceSourceIndex != 0)        ||
      (ResourceSource != NULL)          ||
      ((NameOpNode == NULL) && (NewRdNode == NULL))) {
    ASSERT (0);
    return EFI_INVALID_PARAMETER;
  }

  Status = CheckAddressSpaceFields (
             IsMinFixed,
             IsMaxFixed,
             AddressGranularity,
             AddressMinimum,
             AddressMaximum,
             AddressTranslation,
             RangeLength
             );
  if (EFI_ERROR (Status)) {
    ASSERT (0);
    return Status;
  }

  // Header
  RdDWord.Header.Header.Bits.Name =
    ACPI_LARGE_DWORD_ADDRESS_SPACE_DESCRIPTOR_NAME;
  RdDWord.Header.Header.Bits.Type = ACPI_LARGE_ITEM_FLAG;
  RdDWord.Header.Length = sizeof (EFI_ACPI_DWORD_ADDRESS_SPACE_DESCRIPTOR) -
                               sizeof (ACPI_LARGE_RESOURCE_HEADER);

  // Body
  RdDWord.ResType = ResourceType;
  RdDWord.GenFlag = AddressSpaceGeneralFlags (
                      IsPosDecode,
                      IsMinFixed,
                      IsMaxFixed
                      );
  RdDWord.SpecificFlag = TypeSpecificFlags;
  RdDWord.AddrSpaceGranularity = AddressGranularity;
  RdDWord.AddrRangeMin = AddressMinimum;
  RdDWord.AddrRangeMax = AddressMaximum;
  RdDWord.AddrTranslationOffset = AddressTranslation;
  RdDWord.AddrLen = RangeLength;

  Status = AmlCreateDataNode (
             EAmlNodeDataTypeResourceData,
             (UINT8*)&RdDWord,
             sizeof (EFI_ACPI_DWORD_ADDRESS_SPACE_DESCRIPTOR),
             &RdNode
             );
  if (EFI_ERROR (Status)) {
    ASSERT (0);
    return Status;
  }

  return LinkRdNode (RdNode, NameOpNode, NewRdNode);
}

/** Code generation for the "DWordIO ()" ASL function.

  The Resource Data effectively created is an Extended Interrupt Resource
  Data. Cf ACPI 6.4:
   - s6.4.3.5.2 "DWord Address Space Descriptor".
   - s19.6.34 "DWordIO".

  The created resource data node can be:
   - appended to the list of resource data elements of the NameOpNode.
     In such case NameOpNode must be defined by a the "Name ()" ASL statement
     and initially contain a "ResourceTemplate ()".
   - returned through the NewRdNode parameter.

  @param [in]  IsResourceConsumer   ResourceUsage parameter,
                                    See ACPI 6.4 spec, s19.6.34 for more.
  @param [in]  IsMinFixed           See ACPI 6.4 spec, s19.6.34 for more.
  @param [in]  IsMaxFixed           See ACPI 6.4 spec, s19.6.34 for more.
  @param [in]  IsPosDecode          Decode parameter,
                                    See ACPI 6.4 spec, s19.6.34 for more.
  @param [in]  IsaRanges            Available values are:
                                     0-Reserved
                                     1-NonISAOnly
                                     2-ISAOnly
                                     3-EntireRange
                                    See ACPI 6.4 spec, s19.6.34 for more.
  @param [in]  AddressGranularity   See ACPI 6.4 spec, s19.6.34 for more.
  @param [in]  AddressMinimum       See ACPI 6.4 spec, s19.6.34 for more.
  @param [in]  AddressMaximum       See ACPI 6.4 spec, s19.6.34 for more.
  @param [in]  AddressTranslation   See ACPI 6.4 spec, s19.6.34 for more.
  @param [in]  RangeLength          See ACPI 6.4 spec, s19.6.34 for more.
  @param [in]  ResourceSourceIndex  See ACPI 6.4 spec, s19.6.34 for more.
                                    Not supported.
  @param [in]  ResourceSource       See ACPI 6.4 spec, s19.6.34 for more.
                                    Not supported.
  @param [in]  IsDenseTranslation   TranslationDensity parameter,
                                    See ACPI 6.4 spec, s19.6.34 for more.
  @param [in]  IsTypeStatic         TranslationType parameter,
                                    See ACPI 6.4 spec, s19.6.34 for more.
  @param [in]  NameOpNode           NameOp object node defining a named object.
                                    If provided, append the new resource data
                                    node to the list of resource data elements
                                    of this node.
  @param [out] NewRdNode            If provided and success,
                                    contain the created node.

  @retval EFI_SUCCESS             The function completed successfully.
  @retval EFI_INVALID_PARAMETER   Invalid parameter.
  @retval EFI_OUT_OF_RESOURCES    Could not allocate memory.
**/
EFI_STATUS
EFIAPI
AmlCodeGenRdDWordIo (
  IN        BOOLEAN                 IsResourceConsumer,
  IN        BOOLEAN                 IsMinFixed,
  IN        BOOLEAN                 IsMaxFixed,
  IN        BOOLEAN                 IsPosDecode,
  IN        UINT8                   IsaRanges,
  IN        UINT32                  AddressGranularity,
  IN        UINT32                  AddressMinimum,
  IN        UINT32                  AddressMaximum,
  IN        UINT32                  AddressTranslation,
  IN        UINT32                  RangeLength,
  IN        UINT8                   ResourceSourceIndex,
  IN  CONST CHAR8                   *ResourceSource,
  IN        BOOLEAN                 IsDenseTranslation,
  IN        BOOLEAN                 IsTypeStatic,
  IN        AML_OBJECT_NODE_HANDLE  NameOpNode, OPTIONAL
  OUT       AML_DATA_NODE_HANDLE    *NewRdNode  OPTIONAL
  )
{
  return AmlCodeGenRdDWordSpace (
           ACPI_ADDRESS_SPACE_TYPE_IO,
           IsResourceConsumer,
           IsPosDecode,
           IsMinFixed,
           IsMaxFixed,
           RdIoRangeSpecificFlags (
             IsaRanges,
             IsDenseTranslation,
             IsTypeStatic
             ),
           AddressGranularity,
           AddressMinimum,
           AddressMaximum,
           AddressTranslation,
           RangeLength,
           ResourceSourceIndex,
           ResourceSource,
           NameOpNode,
           NewRdNode
           );
}

/** Code generation for the "DWordMemory ()" ASL function.

  The Resource Data effectively created is an Extended Interrupt Resource
  Data. Cf ACPI 6.4:
   - s6.4.3.5.2 "DWord Address Space Descriptor".
   - s19.6.35 "DWordMemory".

  The created resource data node can be:
   - appended to the list of resource data elements of the NameOpNode.
     In such case NameOpNode must be defined by a the "Name ()" ASL statement
     and initially contain a "ResourceTemplate ()".
   - returned through the NewRdNode parameter.

  @param [in]  IsResourceConsumer   ResourceUsage parameter,
                                    See ACPI 6.4 spec, s19.6.35 for more.
  @param [in]  IsPosDecode          Decode parameter,
                                    See ACPI 6.4 spec, s19.6.35 for more.
  @param [in]  IsMinFixed           See ACPI 6.4 spec, s19.6.35 for more.
  @param [in]  IsMaxFixed           See ACPI 6.4 spec, s19.6.35 for more.
  @param [in]  Cacheable            Available values are:
                                    0-The memory is non-cacheable
                                    1-The memory is cacheable
                                    2-The memory is cacheable and supports
                                      write combining
                                    3-The memory is cacheable and prefetchable
  @param [in]  IsReadWrite          ReadAndWrite parameter,
                                    See ACPI 6.4 spec, s19.6.35 for more.
  @param [in]  AddressGranularity   See ACPI 6.4 spec, s19.6.35 for more.
  @param [in]  AddressMinimum       See ACPI 6.4 spec, s19.6.35 for more.
  @param [in]  AddressMaximum       See ACPI 6.4 spec, s19.6.35 for more.
  @param [in]  AddressTranslation   See ACPI 6.4 spec, s19.6.35 for more.
  @param [in]  RangeLength          See ACPI 6.4 spec, s19.6.35 for more.
  @param [in]  ResourceSourceIndex  See ACPI 6.4 spec, s19.6.35 for more.
                                    Not supported.
  @param [in]  ResourceSource       See ACPI 6.4 spec, s19.6.35 for more.
                                    Not supported.
  @param [in]  MemoryRangeType      Available values are:
                                      0-AddressRangeMemory
                                      1-AddressRangeReserved
                                      2-AddressRangeACPI
                                      3-AddressRangeNVS
                                    See ACPI 6.4 spec, s19.6.35 for more.
  @param [in]  IsTypeStatic         TranslationType parameter,
                                    See ACPI 6.4 spec, s19.6.35 for more.
  @param [in]  NameOpNode           NameOp object node defining a named object.
                                    If provided, append the new resource data
                                    node to the list of resource data elements
                                    of this node.
  @param [out] NewRdNode            If provided and success,
                                    contain the created node.

  @retval EFI_SUCCESS             The function completed successfully.
  @retval EFI_INVALID_PARAMETER   Invalid parameter.
  @retval EFI_OUT_OF_RESOURCES    Could not allocate memory.
**/
EFI_STATUS
EFIAPI
AmlCodeGenRdDWordMemory (
  IN        BOOLEAN                 IsResourceConsumer,
  IN        BOOLEAN                 IsPosDecode,
  IN        BOOLEAN                 IsMinFixed,
  IN        BOOLEAN                 IsMaxFixed,
  IN        UINT8                   Cacheable,
  IN        BOOLEAN                 IsReadWrite,
  IN        UINT32                  AddressGranularity,
  IN        UINT32                  AddressMinimum,
  IN        UINT32                  AddressMaximum,
  IN        UINT32                  AddressTranslation,
  IN        UINT32                  RangeLength,
  IN        UINT8                   ResourceSourceIndex,
  IN  CONST CHAR8                   *ResourceSource,
  IN        UINT8                   MemoryRangeType,
  IN        BOOLEAN                 IsTypeStatic,
  IN        AML_OBJECT_NODE_HANDLE  NameOpNode, OPTIONAL
  OUT       AML_DATA_NODE_HANDLE    *NewRdNode  OPTIONAL
  )
{
  return AmlCodeGenRdDWordSpace (
           ACPI_ADDRESS_SPACE_TYPE_MEM,
           IsResourceConsumer,
           IsPosDecode,
           IsMinFixed,
           IsMaxFixed,
           MemoryRangeSpecificFlags (
             Cacheable,
             IsReadWrite,
             MemoryRangeType,
             IsTypeStatic
             ),
           AddressGranularity,
           AddressMinimum,
           AddressMaximum,
           AddressTranslation,
           RangeLength,
           ResourceSourceIndex,
           ResourceSource,
           NameOpNode,
           NewRdNode
           );
}

/** Code generation for the "WordSpace ()" ASL function.

  The Resource Data effectively created is an Extended Interrupt Resource
  Data. Cf ACPI 6.4:
   - s6.4.3.5.3 "Word Address Space Descriptor".
   - s19.6.151 "WordSpace".

  The created resource data node can be:
   - appended to the list of resource data elements of the NameOpNode.
     In such case NameOpNode must be defined by a the "Name ()" ASL statement
     and initially contain a "ResourceTemplate ()".
   - returned through the NewRdNode parameter.

  @param [in]  ResourceType         See ACPI 6.4 spec, s6.4.3.5.3 for more.
                                    Available values are:
                                      0:        Memory range
                                      1:        I/O range
                                      2:        Bus number range
                                      3-191:    Reserved
                                      192-255:  Hardware Vendor Defined
  @param [in]  IsResourceConsumer   ResourceUsage parameter,
                                    See ACPI 6.4 spec, s19.6.151 for more.
  @param [in]  IsPosDecode          Decode parameter,
                                    See ACPI 6.4 spec, s19.6.151 for more.
  @param [in]  IsMinFixed           See ACPI 6.4 spec, s19.6.151 for more.
  @param [in]  IsMaxFixed           See ACPI 6.4 spec, s19.6.151 for more.
  @param [in]  TypeSpecificFlags    See ACPI 6.4 spec, s6.4.3.5.5
                                    "Resource Type Specific Flags".
  @param [in]  AddressGranularity   See ACPI 6.4 spec, s19.6.151 for more.
  @param [in]  AddressMinimum       See ACPI 6.4 spec, s19.6.151 for more.
  @param [in]  AddressMaximum       See ACPI 6.4 spec, s19.6.151 for more.
  @param [in]  AddressTranslation   See ACPI 6.4 spec, s19.6.151 for more.
  @param [in]  RangeLength          See ACPI 6.4 spec, s19.6.151 for more.
  @param [in]  ResourceSourceIndex  See ACPI 6.4 spec, s19.6.151 for more.
                                    Not supported.
  @param [in]  ResourceSource       See ACPI 6.4 spec, s19.6.151 for more.
                                    Not supported.
  @param [in]  NameOpNode           NameOp object node defining a named object.
                                    If provided, append the new resource data
                                    node to the list of resource data elements
                                    of this node.
  @param [out] NewRdNode            If provided and success,
                                    contain the created node.

  @retval EFI_SUCCESS             The function completed successfully.
  @retval EFI_INVALID_PARAMETER   Invalid parameter.
  @retval EFI_OUT_OF_RESOURCES    Could not allocate memory.
**/
STATIC
EFI_STATUS
EFIAPI
AmlCodeGenRdWordSpace (
  IN        UINT8                   ResourceType,
  IN        BOOLEAN                 IsResourceConsumer,
  IN        BOOLEAN                 IsPosDecode,
  IN        BOOLEAN                 IsMinFixed,
  IN        BOOLEAN                 IsMaxFixed,
  IN        UINT8                   TypeSpecificFlags,
  IN        UINT16                  AddressGranularity,
  IN        UINT16                  AddressMinimum,
  IN        UINT16                  AddressMaximum,
  IN        UINT16                  AddressTranslation,
  IN        UINT16                  RangeLength,
  IN        UINT8                   ResourceSourceIndex,
  IN  CONST CHAR8                   *ResourceSource,
  IN        AML_OBJECT_NODE_HANDLE  NameOpNode, OPTIONAL
  OUT       AML_DATA_NODE_HANDLE    *NewRdNode  OPTIONAL
  )
{
  EFI_STATUS                                Status;
  AML_DATA_NODE                           * RdNode;
  EFI_ACPI_WORD_ADDRESS_SPACE_DESCRIPTOR    Rdword;

  // ResourceSource and ResourceSourceIndex are not supported.
  if ((TypeSpecificFlags == MAX_UINT8)  ||
      (ResourceSourceIndex != 0)        ||
      (ResourceSource != NULL)          ||
      ((NameOpNode == NULL) && (NewRdNode == NULL))) {
    ASSERT (0);
    return EFI_INVALID_PARAMETER;
  }

  Status = CheckAddressSpaceFields (
             IsMinFixed,
             IsMaxFixed,
             AddressGranularity,
             AddressMinimum,
             AddressMaximum,
             AddressTranslation,
             RangeLength
             );
  if (EFI_ERROR (Status)) {
    ASSERT (0);
    return Status;
  }

  // Header
  Rdword.Header.Header.Bits.Name =
    ACPI_LARGE_WORD_ADDRESS_SPACE_DESCRIPTOR_NAME;
  Rdword.Header.Header.Bits.Type = ACPI_LARGE_ITEM_FLAG;
  Rdword.Header.Length = sizeof (EFI_ACPI_WORD_ADDRESS_SPACE_DESCRIPTOR) -
                               sizeof (ACPI_LARGE_RESOURCE_HEADER);

  // Body
  Rdword.ResType = ResourceType;
  Rdword.GenFlag = AddressSpaceGeneralFlags (
                     IsPosDecode,
                     IsMinFixed,
                     IsMaxFixed
                     );
  Rdword.SpecificFlag = TypeSpecificFlags;
  Rdword.AddrSpaceGranularity = AddressGranularity;
  Rdword.AddrRangeMin = AddressMinimum;
  Rdword.AddrRangeMax = AddressMaximum;
  Rdword.AddrTranslationOffset = AddressTranslation;
  Rdword.AddrLen = RangeLength;

  Status = AmlCreateDataNode (
             EAmlNodeDataTypeResourceData,
             (UINT8*)&Rdword,
             sizeof (EFI_ACPI_WORD_ADDRESS_SPACE_DESCRIPTOR),
             &RdNode
             );
  if (EFI_ERROR (Status)) {
    ASSERT (0);
    return Status;
  }

  return LinkRdNode (RdNode, NameOpNode, NewRdNode);
}

/** Code generation for the "WordBusNumber ()" ASL function.

  The Resource Data effectively created is an Extended Interrupt Resource
  Data. Cf ACPI 6.4:
   - s6.4.3.5.3 "Word Address Space Descriptor".
   - s19.6.149 "WordBusNumber".

  The created resource data node can be:
   - appended to the list of resource data elements of the NameOpNode.
     In such case NameOpNode must be defined by a the "Name ()" ASL statement
     and initially contain a "ResourceTemplate ()".
   - returned through the NewRdNode parameter.

  @param [in]  IsResourceConsumer   ResourceUsage parameter,
                                    See ACPI 6.4 spec, s19.6.149 for more.
  @param [in]  IsMinFixed           See ACPI 6.4 spec, s19.6.149 for more.
  @param [in]  IsMaxFixed           See ACPI 6.4 spec, s19.6.149 for more.
  @param [in]  IsPosDecode          Decode parameter,
                                    See ACPI 6.4 spec, s19.6.149 for more.
  @param [in]  AddressGranularity   See ACPI 6.4 spec, s19.6.149 for more.
  @param [in]  AddressMinimum       See ACPI 6.4 spec, s19.6.149 for more.
  @param [in]  AddressMaximum       See ACPI 6.4 spec, s19.6.149 for more.
  @param [in]  AddressTranslation   See ACPI 6.4 spec, s19.6.149 for more.
  @param [in]  RangeLength          See ACPI 6.4 spec, s19.6.149 for more.
  @param [in]  ResourceSourceIndex  See ACPI 6.4 spec, s19.6.149 for more.
                                    Not supported.
  @param [in]  ResourceSource       See ACPI 6.4 spec, s19.6.149 for more.
                                    Not supported.
  @param [in]  NameOpNode           NameOp object node defining a named object.
                                    If provided, append the new resource data
                                    node to the list of resource data elements
                                    of this node.
  @param [out] NewRdNode            If provided and success,
                                    contain the created node.

  @retval EFI_SUCCESS             The function completed successfully.
  @retval EFI_INVALID_PARAMETER   Invalid parameter.
  @retval EFI_OUT_OF_RESOURCES    Could not allocate memory.
**/
EFI_STATUS
EFIAPI
AmlCodeGenRdWordBusNumber (
  IN        BOOLEAN                 IsResourceConsumer,
  IN        BOOLEAN                 IsMinFixed,
  IN        BOOLEAN                 IsMaxFixed,
  IN        BOOLEAN                 IsPosDecode,
  IN        UINT32                  AddressGranularity,
  IN        UINT32                  AddressMinimum,
  IN        UINT32                  AddressMaximum,
  IN        UINT32                  AddressTranslation,
  IN        UINT32                  RangeLength,
  IN        UINT8                   ResourceSourceIndex,
  IN  CONST CHAR8                   *ResourceSource,
  IN        AML_OBJECT_NODE_HANDLE  NameOpNode, OPTIONAL
  OUT       AML_DATA_NODE_HANDLE    *NewRdNode  OPTIONAL
  )
{
  // There is no Type Specific Flags for buses.
  return AmlCodeGenRdWordSpace (
           ACPI_ADDRESS_SPACE_TYPE_BUS,
           IsResourceConsumer,
           IsPosDecode,
           IsMinFixed,
           IsMaxFixed,
           0,
           AddressGranularity,
           AddressMinimum,
           AddressMaximum,
           AddressTranslation,
           RangeLength,
           ResourceSourceIndex,
           ResourceSource,
           NameOpNode,
           NewRdNode
           );
}

/** Code generation for the "QWordSpace ()" ASL function.

  The Resource Data effectively created is an Extended Interrupt Resource
  Data. Cf ACPI 6.4:
   - s6.4.3.5.1 "QWord Address Space Descriptor".
   - s19.6.111 "QWordSpace".

  The created resource data node can be:
   - appended to the list of resource data elements of the NameOpNode.
     In such case NameOpNode must be defined by a the "Name ()" ASL statement
     and initially contain a "ResourceTemplate ()".
   - returned through the NewRdNode parameter.

  @param [in]  ResourceType         See ACPI 6.4 spec, s6.4.3.5.1 for more.
                                    Available values are:
                                      0:        Memory range
                                      1:        I/O range
                                      2:        Bus number range
                                      3-191:    Reserved
                                      192-255:  Hardware Vendor Defined
  @param [in]  IsResourceConsumer   ResourceUsage parameter,
                                    See ACPI 6.4 spec, s19.6.111 for more.
  @param [in]  IsPosDecode          Decode parameter,
                                    See ACPI 6.4 spec, s19.6.111 for more.
  @param [in]  IsMinFixed           See ACPI 6.4 spec, s19.6.111 for more.
  @param [in]  IsMaxFixed           See ACPI 6.4 spec, s19.6.111 for more.
  @param [in]  TypeSpecificFlags    See ACPI 6.4 spec, s6.4.3.5.5
                                    "Resource Type Specific Flags".
  @param [in]  AddressGranularity   See ACPI 6.4 spec, s19.6.111 for more.
  @param [in]  AddressMinimum       See ACPI 6.4 spec, s19.6.111 for more.
  @param [in]  AddressMaximum       See ACPI 6.4 spec, s19.6.111 for more.
  @param [in]  AddressTranslation   See ACPI 6.4 spec, s19.6.111 for more.
  @param [in]  RangeLength          See ACPI 6.4 spec, s19.6.111 for more.
  @param [in]  ResourceSourceIndex  See ACPI 6.4 spec, s19.6.111 for more.
                                    Not supported.
  @param [in]  ResourceSource       See ACPI 6.4 spec, s19.6.111 for more.
                                    Not supported.
  @param [in]  NameOpNode           NameOp object node defining a named object.
                                    If provided, append the new resource data
                                    node to the list of resource data elements
                                    of this node.
  @param [out] NewRdNode            If provided and success,
                                    contain the created node.

  @retval EFI_SUCCESS             The function completed successfully.
  @retval EFI_INVALID_PARAMETER   Invalid parameter.
  @retval EFI_OUT_OF_RESOURCES    Could not allocate memory.
**/
STATIC
EFI_STATUS
EFIAPI
AmlCodeGenRdQWordSpace (
  IN        UINT8                   ResourceType,
  IN        BOOLEAN                 IsResourceConsumer,
  IN        BOOLEAN                 IsPosDecode,
  IN        BOOLEAN                 IsMinFixed,
  IN        BOOLEAN                 IsMaxFixed,
  IN        UINT8                   TypeSpecificFlags,
  IN        UINT64                  AddressGranularity,
  IN        UINT64                  AddressMinimum,
  IN        UINT64                  AddressMaximum,
  IN        UINT64                  AddressTranslation,
  IN        UINT64                  RangeLength,
  IN        UINT8                   ResourceSourceIndex,
  IN  CONST CHAR8                   *ResourceSource,
  IN        AML_OBJECT_NODE_HANDLE  NameOpNode, OPTIONAL
  OUT       AML_DATA_NODE_HANDLE    *NewRdNode  OPTIONAL
  )
{
  EFI_STATUS                                Status;
  AML_DATA_NODE                           * RdNode;
  EFI_ACPI_QWORD_ADDRESS_SPACE_DESCRIPTOR   RdQword;

  // ResourceSource and ResourceSourceIndex are not supported.
  if ((TypeSpecificFlags == MAX_UINT8)  ||
      (ResourceSourceIndex != 0)        ||
      (ResourceSource != NULL)          ||
      ((NameOpNode == NULL) && (NewRdNode == NULL))) {
    ASSERT (0);
    return EFI_INVALID_PARAMETER;
  }

  Status = CheckAddressSpaceFields (
             IsMinFixed,
             IsMaxFixed,
             AddressGranularity,
             AddressMinimum,
             AddressMaximum,
             AddressTranslation,
             RangeLength
             );
  if (EFI_ERROR (Status)) {
    ASSERT (0);
    return Status;
  }

  // Header
  RdQword.Header.Header.Bits.Name =
    ACPI_LARGE_QWORD_ADDRESS_SPACE_DESCRIPTOR_NAME;
  RdQword.Header.Header.Bits.Type = ACPI_LARGE_ITEM_FLAG;
  RdQword.Header.Length = sizeof (EFI_ACPI_QWORD_ADDRESS_SPACE_DESCRIPTOR) -
                               sizeof (ACPI_LARGE_RESOURCE_HEADER);

  // Body
  RdQword.ResType = ResourceType;
  RdQword.GenFlag = AddressSpaceGeneralFlags (
                      IsPosDecode,
                      IsMinFixed,
                      IsMaxFixed
                      );
  RdQword.SpecificFlag = TypeSpecificFlags;
  RdQword.AddrSpaceGranularity = AddressGranularity;
  RdQword.AddrRangeMin = AddressMinimum;
  RdQword.AddrRangeMax = AddressMaximum;
  RdQword.AddrTranslationOffset = AddressTranslation;
  RdQword.AddrLen = RangeLength;

  Status = AmlCreateDataNode (
             EAmlNodeDataTypeResourceData,
             (UINT8*)&RdQword,
             sizeof (EFI_ACPI_QWORD_ADDRESS_SPACE_DESCRIPTOR),
             &RdNode
             );
  if (EFI_ERROR (Status)) {
    ASSERT (0);
    return Status;
  }

  return LinkRdNode (RdNode, NameOpNode, NewRdNode);
}

/** Code generation for the "QWordMemory ()" ASL function.

  The Resource Data effectively created is an Extended Interrupt Resource
  Data. Cf ACPI 6.4:
   - s6.4.3.5.1 "QWord Address Space Descriptor".
   - s19.6.110 "QWordMemory".

  The created resource data node can be:
   - appended to the list of resource data elements of the NameOpNode.
     In such case NameOpNode must be defined by a the "Name ()" ASL statement
     and initially contain a "ResourceTemplate ()".
   - returned through the NewRdNode parameter.

  @param [in]  IsResourceConsumer   ResourceUsage parameter,
                                    See ACPI 6.4 spec, s19.6.110 for more.
  @param [in]  IsPosDecode          Decode parameter,
                                    See ACPI 6.4 spec, s19.6.110 for more.
  @param [in]  IsMinFixed           See ACPI 6.4 spec, s19.6.110 for more.
  @param [in]  IsMaxFixed           See ACPI 6.4 spec, s19.6.110 for more.
  @param [in]  Cacheable            Available values are:
                                    0-The memory is non-cacheable
                                    1-The memory is cacheable
                                    2-The memory is cacheable and supports
                                      write combining
                                    3-The memory is cacheable and prefetchable
  @param [in]  IsReadWrite          ReadAndWrite parameter,
                                    See ACPI 6.4 spec, s19.6.110 for more.
  @param [in]  AddressGranularity   See ACPI 6.4 spec, s19.6.110 for more.
  @param [in]  AddressMinimum       See ACPI 6.4 spec, s19.6.110 for more.
  @param [in]  AddressMaximum       See ACPI 6.4 spec, s19.6.110 for more.
  @param [in]  AddressTranslation   See ACPI 6.4 spec, s19.6.110 for more.
  @param [in]  RangeLength          See ACPI 6.4 spec, s19.6.110 for more.
  @param [in]  ResourceSourceIndex  See ACPI 6.4 spec, s19.6.110 for more.
                                    Not supported.
  @param [in]  ResourceSource       See ACPI 6.4 spec, s19.6.110 for more.
                                    Not supported.
  @param [in]  MemoryRangeType      Available values are:
                                      0-AddressRangeMemory
                                      1-AddressRangeReserved
                                      2-AddressRangeACPI
                                      3-AddressRangeNVS
                                    See ACPI 6.4 spec, s19.6.110 for more.
  @param [in]  IsTypeStatic         TranslationType parameter,
                                    See ACPI 6.4 spec, s19.6.110 for more.
  @param [in]  NameOpNode           NameOp object node defining a named object.
                                    If provided, append the new resource data
                                    node to the list of resource data elements
                                    of this node.
  @param [out] NewRdNode            If provided and success,
                                    contain the created node.

  @retval EFI_SUCCESS             The function completed successfully.
  @retval EFI_INVALID_PARAMETER   Invalid parameter.
  @retval EFI_OUT_OF_RESOURCES    Could not allocate memory.
**/
EFI_STATUS
EFIAPI
AmlCodeGenRdQWordMemory (
  IN        BOOLEAN                 IsResourceConsumer,
  IN        BOOLEAN                 IsPosDecode,
  IN        BOOLEAN                 IsMinFixed,
  IN        BOOLEAN                 IsMaxFixed,
  IN        UINT8                   Cacheable,
  IN        BOOLEAN                 IsReadWrite,
  IN        UINT64                  AddressGranularity,
  IN        UINT64                  AddressMinimum,
  IN        UINT64                  AddressMaximum,
  IN        UINT64                  AddressTranslation,
  IN        UINT64                  RangeLength,
  IN        UINT8                   ResourceSourceIndex,
  IN  CONST CHAR8                   *ResourceSource,
  IN        UINT8                   MemoryRangeType,
  IN        BOOLEAN                 IsTypeStatic,
  IN        AML_OBJECT_NODE_HANDLE  NameOpNode, OPTIONAL
  OUT       AML_DATA_NODE_HANDLE    *NewRdNode  OPTIONAL
  )
{
  return AmlCodeGenRdQWordSpace (
           ACPI_ADDRESS_SPACE_TYPE_MEM,
           IsResourceConsumer,
           IsPosDecode,
           IsMinFixed,
           IsMaxFixed,
           MemoryRangeSpecificFlags (
             Cacheable,
             IsReadWrite,
             MemoryRangeType,
             IsTypeStatic
             ),
           AddressGranularity,
           AddressMinimum,
           AddressMaximum,
           AddressTranslation,
           RangeLength,
           ResourceSourceIndex,
           ResourceSource,
           NameOpNode,
           NewRdNode
           );
}

/** Code generation for the "Interrupt ()" ASL function.

  The Resource Data effectively created is an Extended Interrupt Resource
  Data. Cf ACPI 6.4:
   - s6.4.3.6 "Extended Interrupt Descriptor"
   - s19.6.64 "Interrupt (Interrupt Resource Descriptor Macro)"

  The created resource data node can be:
   - appended to the list of resource data elements of the NameOpNode.
     In such case NameOpNode must be defined by a the "Name ()" ASL statement
     and initially contain a "ResourceTemplate ()".
   - returned through the NewRdNode parameter.

  @param  [in]  ResourceConsumer The device consumes the specified interrupt
                                 or produces it for use by a child device.
  @param  [in]  EdgeTriggered    The interrupt is edge triggered or
                                 level triggered.
  @param  [in]  ActiveLow        The interrupt is active-high or active-low.
  @param  [in]  Shared           The interrupt can be shared with other
                                 devices or not (Exclusive).
  @param  [in]  IrqList          Interrupt list. Must be non-NULL.
  @param  [in]  IrqCount         Interrupt count. Must be non-zero.
  @param  [in]  NameOpNode       NameOp object node defining a named object.
                                 If provided, append the new resource data node
                                 to the list of resource data elements of this
                                 node.
  @param  [out] NewRdNode        If provided and success,
                                 contain the created node.

  @retval EFI_SUCCESS             The function completed successfully.
  @retval EFI_INVALID_PARAMETER   Invalid parameter.
  @retval EFI_OUT_OF_RESOURCES    Could not allocate memory.
**/
EFI_STATUS
EFIAPI
AmlCodeGenRdInterrupt (
  IN  BOOLEAN                 ResourceConsumer,
  IN  BOOLEAN                 EdgeTriggered,
  IN  BOOLEAN                 ActiveLow,
  IN  BOOLEAN                 Shared,
  IN  UINT32                  *IrqList,
  IN  UINT8                   IrqCount,
  IN  AML_OBJECT_NODE_HANDLE  NameOpNode, OPTIONAL
  OUT AML_DATA_NODE_HANDLE    *NewRdNode  OPTIONAL
  )
{
  EFI_STATUS                               Status;

  AML_DATA_NODE                          * RdNode;
  EFI_ACPI_EXTENDED_INTERRUPT_DESCRIPTOR   RdInterrupt;
  UINT32                                 * FirstInterrupt;

  if ((IrqList == NULL) ||
      (IrqCount == 0)   ||
      ((NameOpNode == NULL) && (NewRdNode == NULL))) {
    ASSERT (0);
    return EFI_INVALID_PARAMETER;
  }

  // Header
  RdInterrupt.Header.Header.Bits.Name =
    ACPI_LARGE_EXTENDED_IRQ_DESCRIPTOR_NAME;
  RdInterrupt.Header.Header.Bits.Type = ACPI_LARGE_ITEM_FLAG;
  RdInterrupt.Header.Length = sizeof (EFI_ACPI_EXTENDED_INTERRUPT_DESCRIPTOR) -
                                sizeof (ACPI_LARGE_RESOURCE_HEADER);

  // Body
  RdInterrupt.InterruptVectorFlags = (ResourceConsumer ? BIT0 : 0) |
                                     (EdgeTriggered ? BIT1 : 0)    |
                                     (ActiveLow ? BIT2 : 0)        |
                                     (Shared ? BIT3 : 0);
  RdInterrupt.InterruptTableLength = IrqCount;

  // Get the address of the first interrupt field.
  FirstInterrupt = RdInterrupt.InterruptNumber;

  // Copy the list of interrupts.
  CopyMem (FirstInterrupt, IrqList, (sizeof (UINT32) * IrqCount));

  Status = AmlCreateDataNode (
             EAmlNodeDataTypeResourceData,
             (UINT8*)&RdInterrupt,
             sizeof (EFI_ACPI_EXTENDED_INTERRUPT_DESCRIPTOR),
             &RdNode
             );
  if (EFI_ERROR (Status)) {
    ASSERT (0);
    return Status;
  }

  return LinkRdNode (RdNode, NameOpNode, NewRdNode);
}

/** Code generation for the "Register ()" ASL function.

  The Resource Data effectively created is a Generic Register Descriptor.
  Data. Cf ACPI 6.4:
   - s6.4.3.7 "Generic Register Descriptor".
   - s19.6.114 "Register".

  The created resource data node can be:
   - appended to the list of resource data elements of the NameOpNode.
     In such case NameOpNode must be defined by a the "Name ()" ASL statement
     and initially contain a "ResourceTemplate ()".
   - returned through the NewRdNode parameter.

  @param [in]  AddressSpace    Address space where the register exists.
                               Can be one of I/O space, System Memory, etc.
  @param [in]  BitWidth        Number of bits in the register.
  @param [in]  BitOffset       Offset in bits from the start of the register
                               indicated by the Address.
  @param [in]  Address         Register address.
  @param [in]  AccessSize      Size of data values used when accessing the
                               address space. Can be one of:
                                 0 - Undefined, legacy (EFI_ACPI_6_3_UNDEFINED)
                                 1 - Byte access (EFI_ACPI_6_3_BYTE)
                                 2 - Word access (EFI_ACPI_6_3_WORD)
                                 3 - DWord access (EFI_ACPI_6_3_DWORD)
                                 4 - QWord access (EFI_ACPI_6_3_QWORD)
  @param  [in]  NameOpNode       NameOp object node defining a named object.
                                 If provided, append the new resource data node
                                 to the list of resource data elements of this
                                 node.
  @param  [out] NewRdNode        If provided and success,
                                 contain the created node.

  @retval EFI_SUCCESS             The function completed successfully.
  @retval EFI_INVALID_PARAMETER   Invalid parameter.
  @retval EFI_OUT_OF_RESOURCES    Could not allocate memory.
**/
EFI_STATUS
EFIAPI
AmlCodeGenRdRegister (
  IN  UINT8                   AddressSpace,
  IN  UINT8                   BitWidth,
  IN  UINT8                   BitOffset,
  IN  UINT64                  Address,
  IN  UINT8                   AccessSize,
  IN  AML_OBJECT_NODE_HANDLE  NameOpNode, OPTIONAL
  OUT AML_DATA_NODE_HANDLE    *NewRdNode  OPTIONAL
  )
{
  EFI_STATUS                             Status;
  AML_DATA_NODE                        * RdNode;
  EFI_ACPI_GENERIC_REGISTER_DESCRIPTOR   RdRegister;

  if ((AccessSize > EFI_ACPI_6_3_QWORD)  ||
      ((NameOpNode == NULL) && (NewRdNode == NULL))) {
    ASSERT (0);
    return EFI_INVALID_PARAMETER;
  }

  // Header
  RdRegister.Header.Header.Bits.Name =
    ACPI_LARGE_GENERIC_REGISTER_DESCRIPTOR_NAME;
  RdRegister.Header.Header.Bits.Type = ACPI_LARGE_ITEM_FLAG;
  RdRegister.Header.Length = sizeof (EFI_ACPI_GENERIC_REGISTER_DESCRIPTOR) -
                               sizeof (ACPI_LARGE_RESOURCE_HEADER);

  // Body
  RdRegister.AddressSpaceId = AddressSpace;
  RdRegister.RegisterBitWidth = BitWidth;
  RdRegister.RegisterBitOffset = BitOffset;
  RdRegister.AddressSize = AccessSize;
  RdRegister.RegisterAddress = Address;

  Status = AmlCreateDataNode (
             EAmlNodeDataTypeResourceData,
             (UINT8*)&RdRegister,
             sizeof (EFI_ACPI_GENERIC_REGISTER_DESCRIPTOR),
             &RdNode
             );
  if (EFI_ERROR (Status)) {
    ASSERT (0);
    return Status;
  }

  return LinkRdNode (RdNode, NameOpNode, NewRdNode);
}

/** Code generation for the EndTag resource data.

  The EndTag resource data is automatically generated by the ASL compiler
  at the end of a list of resource data elements. Thus, it doesn't have
  a corresponding ASL function.

  This function allocates memory to create a data node. It is the caller's
  responsibility to either:
   - attach this node to an AML tree;
   - delete this node.

  @param [in]  CheckSum        CheckSum to store in the EndTag.
                               Optional: can be let to 0. It is not
                               updated when new resource data elements
                               are added/removed/modified in the list.
  @param [in]  ParentNode      If not NULL, add the generated node
                               to the end of the variable list of
                               argument of the ParentNode.
                               The ParentNode must not initially contain
                               an EndTag resource data element.
  @param  [out] NewRdNode      If success, contains the generated node.

  @retval EFI_SUCCESS             The function completed successfully.
  @retval EFI_INVALID_PARAMETER   Invalid parameter.
  @retval EFI_OUT_OF_RESOURCES    Could not allocate memory.
**/
EFI_STATUS
EFIAPI
AmlCodeGenEndTag (
  IN  UINT8               CheckSum,   OPTIONAL
  IN  AML_OBJECT_NODE   * ParentNode, OPTIONAL
  OUT AML_DATA_NODE    ** NewRdNode   OPTIONAL
  )
{
  EFI_STATUS                      Status;
  AML_DATA_NODE                 * RdNode;
  EFI_ACPI_END_TAG_DESCRIPTOR     EndTag;
  ACPI_SMALL_RESOURCE_HEADER      SmallResHdr;

  if ((ParentNode == NULL) && (NewRdNode == NULL)) {
    ASSERT (0);
    return EFI_INVALID_PARAMETER;
  }

  RdNode = NULL;

  // Header
  SmallResHdr.Bits.Length = sizeof (EFI_ACPI_END_TAG_DESCRIPTOR) -
                              sizeof (ACPI_SMALL_RESOURCE_HEADER);
  SmallResHdr.Bits.Name = ACPI_SMALL_END_TAG_DESCRIPTOR_NAME;
  SmallResHdr.Bits.Type = ACPI_SMALL_ITEM_FLAG;

  // Body
  EndTag.Desc = SmallResHdr.Byte;
  EndTag.Checksum = CheckSum;

  Status = AmlCreateDataNode (
             EAmlNodeDataTypeResourceData,
             (UINT8*)&EndTag,
             sizeof (EFI_ACPI_END_TAG_DESCRIPTOR),
             &RdNode
             );
  if (EFI_ERROR (Status)) {
    ASSERT (0);
    return Status;
  }

  if (NewRdNode != NULL) {
    *NewRdNode = RdNode;
  }

  if (ParentNode != NULL) {
    // Check the BufferOp doesn't contain any resource data yet.
    // This is a hard check: do not allow to add an EndTag if the BufferNode
    // already has resource data elements attached. Indeed, the EndTag should
    // have already been added.
    if (AmlGetNextVariableArgument ((AML_NODE_HEADER*)ParentNode, NULL) !=
          NULL) {
      ASSERT (0);
      Status = EFI_INVALID_PARAMETER;
      goto error_handler;
    }

    // Manually add the EndTag RdNode. Indeed, the AmlAppendRdNode function
    // is looking for an EndTag, which we are adding here.
    Status = AmlVarListAddTail (
               (AML_NODE_HEADER*)ParentNode,
               (AML_NODE_HEADER*)RdNode
               );
    if (EFI_ERROR (Status)) {
      ASSERT (0);
      goto error_handler;
    }
  }

  return Status;

error_handler:
  if (RdNode != NULL) {
    AmlDeleteTree ((AML_NODE_HEADER*)RdNode);
  }
  return Status;
}

// DEPRECATED APIS
#ifndef DISABLE_NEW_DEPRECATED_INTERFACES

/** DEPRECATED API

  Add an Interrupt Resource Data node.

  This function creates a Resource Data element corresponding to the
  "Interrupt ()" ASL function, stores it in an AML Data Node.

  It then adds it after the input CurrRdNode in the list of resource data
  element.

  The Resource Data effectively created is an Extended Interrupt Resource
  Data. See ACPI 6.3 specification, s6.4.3.6 "Extended Interrupt Descriptor"
  for more information about Extended Interrupt Resource Data.

  The Extended Interrupt contains one single interrupt.

  This function allocates memory to create a data node. It is the caller's
  responsibility to either:
   - attach this node to an AML tree;
   - delete this node.

  Note: The _CRS node must be defined using the ASL Name () function.
        e.g. Name (_CRS, ResourceTemplate () {
               ...
             }

  @ingroup UserApis

  @param  [in]  NameOpCrsNode    NameOp object node defining a "_CRS" object.
                                 Must have an OpCode=AML_NAME_OP, SubOpCode=0.
                                 NameOp object nodes are defined in ASL
                                 using the "Name ()" function.
  @param  [in]  ResourceConsumer The device consumes the specified interrupt
                                 or produces it for use by a child device.
  @param  [in]  EdgeTriggered    The interrupt is edge triggered or
                                 level triggered.
  @param  [in]  ActiveLow        The interrupt is active-high or active-low.
  @param  [in]  Shared           The interrupt can be shared with other
                                 devices or not (Exclusive).
  @param  [in]  IrqList          Interrupt list. Must be non-NULL.
  @param  [in]  IrqCount         Interrupt count. Must be non-zero.


  @retval EFI_SUCCESS             The function completed successfully.
  @retval EFI_INVALID_PARAMETER   Invalid parameter.
  @retval EFI_OUT_OF_RESOURCES    Could not allocate memory.
**/
EFI_STATUS
EFIAPI
AmlCodeGenCrsAddRdInterrupt (
  IN  AML_OBJECT_NODE_HANDLE  NameOpCrsNode,
  IN  BOOLEAN                 ResourceConsumer,
  IN  BOOLEAN                 EdgeTriggered,
  IN  BOOLEAN                 ActiveLow,
  IN  BOOLEAN                 Shared,
  IN  UINT32                * IrqList,
  IN  UINT8                   IrqCount
  )
{
  return AmlCodeGenRdInterrupt (
           ResourceConsumer,
           EdgeTriggered,
           ActiveLow,
           Shared,
           IrqList,
           IrqCount,
           NameOpCrsNode,
           NULL
           );
}

#endif // DISABLE_NEW_DEPRECATED_INTERFACES
