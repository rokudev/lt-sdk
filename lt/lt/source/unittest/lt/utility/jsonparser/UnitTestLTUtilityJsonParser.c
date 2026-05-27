/*******************************************************************************
 * source/unittest/lt/utility/json/UnitTestLTUtilityJsonParser.c
 *    __  __      _ __ ______          __  __  ________  ____  _ ___ __
 *   / / / /___  (_) //_  __/__  _____/ /_/ / /_  __/ / / / /_(_) (_) /___  __
 *  / / / / __ \/ / __// / / _ \/ ___/ __/ /   / / / / / / __/ / / / __/ / / /
 * / /_/ / / / / / /_ / / /  __(__  ) /_/ /___/ / / /_/ / /_/ / / / /_/ /_/ /
 * \____/_/ /_/_/\__//_/  \___/____/\__/_____/_/  \____/\__/_/_/_/\__/\__, /
 *                                                                   /____/
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/utility/jsonparser/LTUtilityJsonParser.h>
#include <tilt/JiltEngine.h>

#define JsonString(x...) #x

/*******************************************************************************
 * Static Variables
 ******************************************************************************/
static const char * s_exampleStr = JsonString(
    { "config": { "device": [
                              {
                           "device class": "LTDevicePushbutton",
                                 "driver": [      {
                                               "name": "Esp32DriverPushButton",
                                               "unit": { "0": { "device class": "LTDevicePins",
                                                                 "device unit": 1,
                                                                        "name": "sidebutton",
                                                                 "implemented": true
                                            }     }    }      ]
                              },
                              {
                           "device class": "LTDevicePins",
                                 "driver": [      {
                                               "name": "Esp32DriverPins",
                                               "implemented": false,
                                               "unit": { "0": { "gpio": 19, "name": "led"    },
                                                         "1": { "gpio": 16, "name": "relay"  },
                                                         "2": { "gpio": 15, "name": "button" }
    }           }           ] }            ]       }   }

);

static JiltEngine * s_engine;

/*******************************************************************************
 * Test Functions
 ******************************************************************************/

/* TestValueTypeToString: verify that the parser correctly returns the value type of a value for a given key
Also use t*/
static void TestValueTypeToString (Tilt *tilt) {
    LTUtilityJsonParser       *parser = lt_createobject(LTUtilityJsonParser);
    LTUtilityJsonParser_Value  value;

    parser->API->GetValue(parser, s_exampleStr, "/config/device/0/device class", &value);
    TILT_EXPECT_TRUE(tilt, lt_strcmp(parser->API->ValueTypeToString(value.type), "String") == 0, "ValToStr.str");
    TILT_EXPECT_FALSE(tilt, lt_strcmp(parser->API->ValueTypeToString(value.type), "Integer") == 0, "ValToStr.str2");
    parser->API->GetValue(parser, s_exampleStr, "/config/device/0/driver/0/unit/0/device unit", &value);
    TILT_EXPECT_TRUE(tilt, lt_strcmp(parser->API->ValueTypeToString(value.type), "Integer") == 0, "ValToStr.int");
    TILT_EXPECT_FALSE(tilt, lt_strcmp(parser->API->ValueTypeToString(value.type), "String") == 0, "ValToStr.int2");
    parser->API->GetValue(parser, s_exampleStr, "/config/device/1/driver/0/implemented", &value);
    TILT_EXPECT_TRUE(tilt, lt_strcmp(parser->API->ValueTypeToString(value.type), "False") == 0, "ValToStr.fal");
    TILT_EXPECT_FALSE (tilt, lt_strcmp(parser->API->ValueTypeToString(value.type), "Integer") == 0, "ValToStr.fal2");

    lt_destroyobject(parser);
}

static void TestIsString (Tilt *tilt) {
    LTUtilityJsonParser       *parser = lt_createobject(LTUtilityJsonParser);
    LTUtilityJsonParser_Value value;

    parser->API->GetValue(parser, s_exampleStr, "/config/device/0/device class", &value);
    TILT_EXPECT_TRUE(tilt, LTUtilityJsonParser_Value_IsString(&value), "IsString.str");

    parser->API->GetValue(parser, s_exampleStr, "/config/device/0/driver/0/unit/0/device unit", &value);
    TILT_EXPECT_FALSE(tilt, LTUtilityJsonParser_Value_IsString(&value), "IsString.int");

    parser->API->GetValue(parser, s_exampleStr, "/config/device/1/driver/0/implemented", &value);
    TILT_EXPECT_FALSE(tilt, LTUtilityJsonParser_Value_IsString(&value), "IsString.false");

    lt_destroyobject(parser);
}

static void TestIsInteger (Tilt *tilt) {
    LTUtilityJsonParser       *parser = lt_createobject(LTUtilityJsonParser);
    LTUtilityJsonParser_Value value;

    parser->API->GetValue(parser, s_exampleStr, "/config/device/0/driver/0/unit/0/device unit", &value);
    TILT_EXPECT_TRUE (tilt, LTUtilityJsonParser_Value_IsInteger(&value), "IsInt.int");

    parser->API->GetValue(parser, s_exampleStr, "/config/device/0/device class", &value);
    TILT_EXPECT_FALSE(tilt, LTUtilityJsonParser_Value_IsInteger(&value), "IsInt.str");

    parser->API->GetValue(parser, s_exampleStr, "/config/device/1/driver/0/implemented", &value);
    TILT_EXPECT_FALSE(tilt, LTUtilityJsonParser_Value_IsInteger(&value), "IsInt.false");

    lt_destroyobject(parser);
}

static void TestIsTrue (Tilt *tilt) {
    LTUtilityJsonParser       *parser = lt_createobject(LTUtilityJsonParser);
    LTUtilityJsonParser_Value value;

    parser->API->GetValue(parser, s_exampleStr, "/config/device/0/driver/0/unit/0/implemented", &value);
    TILT_EXPECT_TRUE(tilt, LTUtilityJsonParser_Value_IsTrue(&value), "Istrue.true");

    parser->API->GetValue(parser, s_exampleStr, "/config/device/1/driver/0/implemented", &value);
    TILT_EXPECT_FALSE(tilt, LTUtilityJsonParser_Value_IsTrue(&value), "Istrue.false");

    parser->API->GetValue(parser, s_exampleStr, "/config/device/0/driver/0/unit/0/device unit", &value);
    TILT_EXPECT_FALSE(tilt, LTUtilityJsonParser_Value_IsTrue(&value), "Istrue.int");

    parser->API->GetValue(parser, s_exampleStr, "/config/device/0/device class", &value);
    TILT_EXPECT_FALSE(tilt, LTUtilityJsonParser_Value_IsTrue(&value), "Istrue.str");

    lt_destroyobject(parser);
}

static void TestIsFalse (Tilt *tilt) {
    LTUtilityJsonParser       *parser = lt_createobject(LTUtilityJsonParser);
    LTUtilityJsonParser_Value value;

    parser->API->GetValue(parser, s_exampleStr, "/config/device/1/driver/0/implemented", &value);
    TILT_EXPECT_TRUE(tilt, LTUtilityJsonParser_Value_IsFalse(&value), "Isfalse.false");

    parser->API->GetValue(parser, s_exampleStr, "/config/device/0/driver/0/unit/0/implemented", &value);
    TILT_EXPECT_FALSE(tilt, LTUtilityJsonParser_Value_IsFalse(&value), "Isfalse.true");

    parser->API->GetValue(parser, s_exampleStr, "/config/device/0/driver/0/unit/0/device unit", &value);
    TILT_EXPECT_FALSE(tilt, LTUtilityJsonParser_Value_IsFalse(&value), "Isfalse.int");

    parser->API->GetValue(parser, s_exampleStr, "/config/device/0/device class", &value);
    TILT_EXPECT_FALSE(tilt, LTUtilityJsonParser_Value_IsFalse(&value), "Isfalse.str");

    lt_destroyobject(parser);
}

static const TiltEngineTest s_tests[] = {
    { TestIsString,          "IsString",          "Test if a provided value is of type kLTUtilityJsonParser_ValueType_String.",  0 },
    { TestIsInteger,         "IsInteger",         "Test if a provided value is of type kLTUtilityJsonParser_ValueType_Integer.", 0 },
    { TestIsTrue,            "IsTrue",            "Test if a provided value is of type kLTUtilityJsonParser_ValueType_True.",    0 },
    { TestIsFalse,           "IsFalse",           "Test if a provided value is of type kLTUtilityJsonParser_ValueType_False.",   0 },
    { TestValueTypeToString, "ValuetypeToString", "Verify the value type returned by parser for a given key",                    0 }
};

static int UnitTestLTUtilityJsonParserImpl_Run(int argc, const char **argv) {
    /* Set test properties */
    s_engine->API->ConfigureTestSuite(s_engine, s_tests, sizeof(s_tests)/sizeof(s_tests[0]), NULL);

    /* Invoke testing */
    return s_engine->API->RunTestSuite(s_engine, argc, argv);
}

/*******************************************************************************
 * Library Initialization
 ******************************************************************************/

static bool UnitTestLTUtilityJsonParserImpl_LibInit(void) {
    s_engine = lt_createobject(JiltEngine);
    return s_engine != NULL;
}

static void UnitTestLTUtilityJsonParserImpl_LibFini(void) {
    lt_destroyobject(s_engine);
}

/*******************************************************************************
 * Library Root Interface Binding
 ******************************************************************************/

typedef_LTLIBRARY_ROOT_INTERFACE(UnitTestLTUtilityJsonParser, 1) LTLIBRARY_EMPTY_INTERFACE;

define_LTLIBRARY_ROOT_INTERFACE(UnitTestLTUtilityJsonParser, UnitTestLTUtilityJsonParserImpl_Run, 1536) LTLIBRARY_DEFINITION;
