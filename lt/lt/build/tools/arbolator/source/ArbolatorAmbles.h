/******************************************************************************
 * ArbolatorAmbles.h - preamble and postamble strings of arbolated output
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

static const char *s_makefileHeaderPreamble  = "#################################################################################\n"
                                               "#  File:           %s\n"
                                               "#  arbolated from: %s\n"
                                               "#\n"
                                               "#  Copyright %d, Roku, Inc.  All rights reserved.\n"
                                               "#################################################################################\n"
                                               "\n";

static const char *s_makeFileFooterPostamble = "\n"
                                               "#################################################################################\n"
                                               "#  LOG\n"
                                               "#################################################################################\n"
                                               "#  %s   arbolator    arbolated\n";


static const char *s_sourceFileHeaderPreamble ="/********************************************************************************\n"
                                               " *  File:           %s\n"
                                               " *  arbolated from: %s\n"
                                               " *\n"
                                               " *  Copyright %d, Roku, Inc.  All rights reserved.\n"
                                               " ********************************************************************************/\n"
                                               "\n";

static const char *s_sourceFileFooterPostamble =
                                               "\n"
                                               "/********************************************************************************\n"
                                               " *  LOG\n"
                                               " ********************************************************************************\n"
                                               " *  %s   arbolator    arbolated\n"
                                               " */\n";

static const char *s_arbolatedTreePreamble =   "#if defined (LTLIBRARY_ARBOLATED_RESOURCE_TREE)\n" \
                                               "  #if LTLIBRARY_ARBOLATED_RESOURCE_TREE == 0\n" \
                                               "    #error do not #include <lt/LTTypes.h> before including this file.\n" \
                                               "  #else\n" \
                                               "    #error LTLIBRARY_ARBOLATED_RESOURCE_TREE already defined.  Only one permitted per library.\n" \
                                               "  #endif\n" \
                                               "#else\n" \
                                               "  #define LTLIBRARY_ARBOLATED_RESOURCE_TREE s_arbolatedResourceTree_%s\n" \
                                               "#endif\n" \
                                               "\n" \
                                               "#include <lt/LTTypes.h>\n" \
                                               "\n" \
                                               "static const u8 s_arbolatedResourceTree_%s[%lu] = {\n"
                                               "\n";

static const char *s_arbolatedTreePostamble =  "};\n";

static const char * s_includePassZeroPreamble   = "/*************\n * source json: ";
static const char * s_includePassZeroPostamble  = "\n */";

static const char * s_arbolatedIncludePreamble  =  "\n"  "/*#lt-include %d.%02d: %s --> %s */\n";
static const char * s_arbolatedIncludePostamble =       "/*#lt-include %d.%02d  END */\n";


/******************************************************************************
 *  LOG
 ******************************************************************************
 *  16-Feb-25   augustus    created
 *  23-Feb-25   augustus    added include and pass 0 pre/postambles
 */