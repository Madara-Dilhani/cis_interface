#include <iostream>
// Include methods for input/output channels
#include "CisInterface.hpp"

int main(int argc, char *argv[]) {
  // Initialize input/output channels
  CisObjInput in_channel("inputA");
  CisObjOutput out_channel("outputA");

  // Declare resulting variables and create buffer for received message
  int flag = 1;
  obj_t p = init_obj();

  // Loop until there is no longer input or the queues are closed
  while (flag >= 0) {
  
    // Receive input from input channel
    // If there is an error, the flag will be negative
    // Otherwise, it is the size of the received message
    flag = in_channel.recv(1, &p);
    if (flag < 0) {
      std::cout << "Model A: No more input." << std::endl;
      break;
    }

    // Print received message
    printf("Model A: (%d verts, %d faces)\n", p.nvert, p.nface);
    int i, j;
    printf("  Vertices:\n");
    for (i = 0; i < p.nvert; i++) {
      printf("   %f, %f, %f\n",
	     p.vertices[i][0], p.vertices[i][1], p.vertices[i][2]);
    }
    printf("  Faces:\n");
    for (i = 0; i < p.nface; i++) {
      printf("   %d, %d, %d\n",
	     p.faces[i][0], p.faces[i][1], p.faces[i][2]);
    }

    // Send output to output channel
    // If there is an error, the flag will be negative
    flag = out_channel.send(1, p);
    if (flag < 0) {
      std::cout << "Model A: Error sending output." << std::endl;
      break;
    }

  }
  
  // Free dynamically allocated structure
  free_obj(&p);
  
  return 0;
}
