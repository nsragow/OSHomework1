# OSHomework1
1) Team:
  Joseph Sklar- 800186021 
  Noah Sragow-
  Jacob Stern-
2) Division of Labor:
  Joseph Sklar- Parts 1 and 2
  Noah Sragow- Parts 1 and 3
  Jacob Stern- Part 4
3) Important Information/Structures in Server:
    Request: Represents a request made to the server, keeps track of important pieces of
    information neccessary to properly pass the information through to the web() method as well
    as important statistical categories necessary for part 3.
    Buffer: Reperesents a buffer needed to hold requests as they wait to be recieved by threads. 
    It also records important statistical categories necessary for part 3.
    
    Our threads are created in main and run either producer() or consumer().
    
    Important reconfiguration: In order to properly identify whether a request was for a text or an image
    we moved much of the code that used to be in web() had to be moved to producer().
    
    ANY implements FIFO.
    
    We tested the scheduling methods using clients that made many calls the server via a forloop. 
    As far as we know there are no bugs.
  
 4) Important Information/Structures in client.
 
    
