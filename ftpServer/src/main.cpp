#include "./headers/stdafx.h"
#include "./headers/controller.h"

int main(int argc, char *argv[])
{
	serverController controller(argc, argv);
	return controller.init();
}
