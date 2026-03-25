Your goal is to make:

GMMU=0 AM_DEBUG=2 DEBUG=5 USE_BOT=1 PYTHONPATH="." AMD=1 AMD_IFACE=USB python3 test/test_tiny.py TestTiny.test_plus

work with the custom firmware in clean/src/main.c


You can run the local tests with:

make -C clean flash && python3 test_tinygrad_flow.py --no-stop --timeout=10

Currently some of these tests are failing.


Goals:
* Fix the tests.
* Make the main tinygrad USB interface work.
* Document device registers in registers.h.
