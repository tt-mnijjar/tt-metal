# Every variable in subdir must be prefixed with subdir (emulating a namespace)

TT_METAL_UNIT_TESTS_FAST_DISPATCH_SRCS_HOME = tests/tt_metal/tt_metal/unit_tests_fast_dispatch

TT_METAL_UNIT_TESTS_FAST_DISPATCH = ${TT_METAL_UNIT_TESTS_FAST_DISPATCH_SRCS_HOME}/tests_main.cpp
TT_METAL_UNIT_TESTS_FAST_DISPATCH += $(wildcard ${TT_METAL_UNIT_TESTS_FAST_DISPATCH_SRCS_HOME}/*/*.cpp)
TT_METAL_UNIT_TESTS_FAST_DISPATCH += $(wildcard ${TT_METAL_UNIT_TESTS_FAST_DISPATCH_SRCS_HOME}/*/*/*.cpp)

TT_METAL_UNIT_TESTS_FAST_DISPATCH_OBJ_HOME = tt_metal/tests/unit_tests_fast_dispatch/
TT_METAL_UNIT_TESTS_FAST_DISPATCH_SRCS = $(patsubst $(TT_METAL_UNIT_TESTS_FAST_DISPATCH_SRCS_HOME)%, $(TT_METAL_UNIT_TESTS_FAST_DISPATCH_OBJ_HOME)%, $(TT_METAL_UNIT_TESTS_FAST_DISPATCH))

TT_METAL_UNIT_TESTS_FAST_DISPATCH_INCLUDES = $(TEST_INCLUDES) $(TT_METAL_INCLUDES) -I$(TT_METAL_HOME)/tests/tt_metal/tt_metal/unit_tests_fast_dispatch/common
TT_METAL_UNIT_TESTS_FAST_DISPATCH_LDFLAGS = -ltt_metal -ldl -lstdc++fs -pthread -lyaml-cpp -lgtest -lgtest_main -ltracy

TT_METAL_UNIT_TESTS_FAST_DISPATCH_OBJS = $(addprefix $(OBJDIR)/, $(TT_METAL_UNIT_TESTS_FAST_DISPATCH_SRCS:.cpp=.o))
TT_METAL_UNIT_TESTS_FAST_DISPATCH_DEPS = $(addprefix $(OBJDIR)/, $(TT_METAL_UNIT_TESTS_FAST_DISPATCH_SRCS:.cpp=.d))

-include $(TT_METAL_UNIT_TESTS_FAST_DISPATCH_DEPS)

# Each module has a top level target as the entrypoint which must match the subdir name
tests/tt_metal/unit_tests_fast_dispatch: $(TESTDIR)/tt_metal/unit_tests_fast_dispatch

.PRECIOUS: $(TESTDIR)/tt_metal/unit_tests_fast_dispatch
$(TESTDIR)/tt_metal/unit_tests_fast_dispatch: $(TT_METAL_UNIT_TESTS_FAST_DISPATCH_OBJS) $(TT_METAL_LIB) $(TT_DNN_LIB)
	@mkdir -p $(@D)
	$(CXX) $(CFLAGS) $(CXXFLAGS) $(TT_METAL_UNIT_TESTS_FAST_DISPATCH_INCLUDES) -o $@ $^ $(LDFLAGS) $(TT_METAL_UNIT_TESTS_FAST_DISPATCH_LDFLAGS)

.PRECIOUS: $(OBJDIR)/$(TT_METAL_UNIT_TESTS_FAST_DISPATCH_OBJ_HOME)/%.o
$(OBJDIR)/$(TT_METAL_UNIT_TESTS_FAST_DISPATCH_OBJ_HOME)/%.o: $(TT_METAL_UNIT_TESTS_FAST_DISPATCH_SRCS_HOME)/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CFLAGS) $(CXXFLAGS) $(TT_METAL_UNIT_TESTS_FAST_DISPATCH_INCLUDES) -c -o $@ $<