# deep_fry
A mutant cross between garish image filters and peer-to-peer filesharing.

## Authors
DEEP Fry was created by Alberto Munoz and Matthew Gao.

The UI and socket header files were created by Charlie Curtsinger.

## Installation and Configuration
This project uses the GraphicsMagick Core API, so it needs to be installed if you plan to contribute to this open-source project. You can find the GraphicsMagick Core C API and its installation instructions at http://www.graphicsmagick.org/api/api.html 

Don't forget to follow the installation instructions for GraphicsMagick respective to the OS you're currently on.

Otherwise, clone this repo, navigate into the directory, and use 'chmod 700 p2psnap' in the terminal to make the file executable. 

Note that the executable file in its current form on this repo was compiled on a Linux terminal. If you are on MacOS or Windows you should run 'make clean' and rerun 'make' in the terminal so you can have an executable that is compiled locally on your own computer. That probably means that even if you intend to use this program without any contributions on any other OS, you need to download the GraphicsMagick API before you rerun 'make' in the terminal.

## How to use
In the same directory as the repo, run './p2psnap receive_folder.' 'receive_folder' will be the directory in which received images from other clients and parents will appear. It is preferable and convenient to make a new directory named 'receive_folder' in the same directory as the repository specifically for receiving files, and using that as the default receive folder for this program. 

To run the client side, also use './p2psnap receive_folder'. For the third and fourth arguments, respectively, enter the machine's name within the local network as well as the port number of the any of the hosts in the network. A port number is given to you when you start up the program for both server and clients.

## Sending files
On successful run of the program, a UI should appear in the terminal. Enter in an existing path to an image (.png, .jpg, .jpeg) in this way: 

> path/image_file.

The file can be followed by the four characters 'f', 's', 'o', 'i', each separated by whitespace. The commands are respectively to 'deep-fry' the image, swirl the image around the center with a random degree of rotation, oil-paint the image, and implode the image, which 'crashes' the image into the center. Another command, :quit or :q, will quit the program and have the machine exit the network. 

In theory, there is no limit to the number of filters one can apply to an image before sending it, but keep in mind that more filters will take more time for the machine to process. Furthermore, it will take more time for the machine to apply filters to images with larger resolutions. 

If you apply at least one filter to an image, it will be saved to your own designated receiving folder named as 'userx.png', where x denotes the number of filtered images you have sent in total. Images you receive from others in the running network shall be denoted as 'clientx_y.png', where x denotes the client's socket file descriptor (for ID purposes) and y denotes the number of images that client has sent in the network session. 

## Example Use
./p2psnap receive_folder

Connection info: Peer port number #####

chatroom: A new client has connected!

client7_0.png: file received from client

user: sample.png

chatroom: Client has disconnected.
