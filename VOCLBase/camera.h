class Camera{
public:
    int pos[3];
    int dir[3];
    float fov;
    float clipNear;
    Camera();
};

Camera::Camera (){
    pos[0] = 0;
    pos[1] = 0;
    pos[2] = 50;
    
    dir[0] = 1;
    dir[1] = 1;
    dir[2] = -2;
    
    fov = 60;
    
    
}