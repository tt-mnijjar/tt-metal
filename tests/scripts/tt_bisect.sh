#!/bin/bash

: << 'END'
This script is used to find the commit that broke a test.
Flags:
    -f | --file : test file to run, also the test that broke
    -s | --slow : indicate the test runs in slow dispatch
    -g | --good : good commit to start bisect
    -b | --bad : bad commit to start bisect
Example:
    ./tests/scripts/tt_bisect.sh -s -f ./build/test/tt_metal/test_add_two_ints -b HEAD -g 1eb7930
If the test involves multiple words you have to do "test_file":
    ./tests/scripts/tt_bisect.sh -s -f "pytest $TT_METAL_HOME/models/demos/resnet/tests/test_resnet18.py" -b HEAD -g 1eb7930
    ./tests/scripts/tt_bisect.sh -s -f "python tests/scripts/run_tt_metal.py --dispatch-mode fast" -b HEAD -g HEAD~10
END

cd $TT_METAL_HOME
source build/python_env/bin/activate
export PYTHONPATH=$TT_METAL_HOME
unset TT_METAL_SLOW_DISPATCH_MODE

timeout_duration=2m
while getopts "f:sg:b:t:" opt; do
    case $opt in
         f | file)
            test=$OPTARG
            ;;
         s | slow)
            sd=true
            ;;
         g | good)
            good_commit=$OPTARG
            ;;
         b | bad)
            bad_commit=$OPTARG
            ;;
         t | timeout)
            timeout_duration=$OPTARG
            ;;
         \?)
            echo "Invalid option: -$OPTARG" >&2
            exit 1
            ;;
    esac
done

if ([ -z "$test" ] || [ -z "$good_commit" ] || [ -z "$bad_commit" ]); then
    echo "Please specify a test file, good commit and bad commit"
    exit 1
fi

if [[ $sd ]]; then
   echo "Running in slow dispatch mode"
   export TT_METAL_SLOW_DISPATCH_MODE=1
fi

echo "Time to find who broke it :)"
echo "Good commit:" $good_commit
echo "Bad commit:" $bad_commit

found=false
build_code=0

git bisect start $bad_commit $good_commit --

while [[ "$found" = "false" ]]; do
   echo "at commit `git rev-parse HEAD`"
   echo "Make clean"
   make clean; build_code+=$?

   echo "Making build"
   make build; build_code+=$?

   echo "Making tests"
   make tests; build_code+=$?

   if [[ $build_code -ne 0 ]]; then
      echo "Build failed"
      git bisect skip
      continue
   fi

   timeout $timeout_duration $test
   timeout_code=${PIPESTATUS[0]}
   echo $timeout_code

   if [ $timeout_code -eq 0 ]; then
      first_line=$(git bisect good | head -n 1)
   elif [ $timeout_code -eq 124 ]; then
      echo `git rev-parse HEAD` > ~/bad_commit.txt
      break
   else
      first_line=$(git bisect bad | head -n 1)
   fi

   if [[ $first_line == *"is the first bad commit"* ]]; then
      echo "FOUND IT!: " $first_line
      found=true
   fi
done
git bisect reset

if [ $timeout_code -eq 124 ]; then
   echo "Test has hung, need to reset the board"
   exit 124
fi
