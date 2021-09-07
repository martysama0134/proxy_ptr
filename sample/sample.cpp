#include "../include/recursive_ptr.h"


int main()
{
	using namespace recursive_ptr;
	auto testptr = make_recursive<char>(1);
	auto testptr2 = testptr;

	for(int i=0; i < 500; i++)
		auto testptr3= testptr;
}