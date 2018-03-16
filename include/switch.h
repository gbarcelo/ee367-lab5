/*
 * switch.h
 */

enum boolean {
  FALSE,
  TRUE
};

 struct connections {
   int valid;
   int host;
   int port;
 };

 void switch_main(int switch_id);
