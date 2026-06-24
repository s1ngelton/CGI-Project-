#include "Scene.h"
#include "Primitives.h"
#include <iostream>

void Scene::addCube(const std::string& name, glm::vec3 pos, glm::vec3 scale, glm::vec3 color, float roughness, float metallic) {
    SceneObject obj;
    obj.name = name;
    obj.vao = Primitives::getCubeVAO();
    obj.vertexCount = 36;
    
    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, pos);
    model = glm::scale(model, scale);
    obj.transform = model;
    
    obj.color = color;
    obj.roughness = roughness;
    obj.metallic = metallic;
    objects.push_back(obj);
}

void Scene::load(const std::string& path) {
    std::cout << "Building procedural floorplan instead of loading " << path << "\n";
    buildFloorplan();
}

void Scene::buildFloorplan() {
    glm::vec3 wallColor = glm::vec3(0.8f, 0.8f, 0.8f);
    glm::vec3 floorColor = glm::vec3(0.2f, 0.15f, 0.1f); // Wood-ish
    
    // Floor (wood - slightly glossy: roughness 0.4)
    addCube("Floor", glm::vec3(0, -0.1f, 0), glm::vec3(10.0f, 0.1f, 10.0f), floorColor, 0.4f, 0.0f);
    // Ceiling (matte plaster)
    addCube("Ceiling", glm::vec3(0, 3.1f, 0), glm::vec3(10.0f, 0.1f, 10.0f), wallColor, 0.9f, 0.0f);

    // Ceiling Lamp (hanging down slightly in the living room where sofa and TV are)
    addCube("Ceiling_Lamp", glm::vec3(2.0f, 2.95f, 6.0f), glm::vec3(0.3f, 0.05f, 0.3f), glm::vec3(2.0f, 2.0f, 1.8f), 0.1f, 0.9f);

    // Outer Walls (Thickness: 0.2f, Height: 3.0f - matte plaster)
    addCube("Wall_North", glm::vec3(0, 1.5f, -10.1f), glm::vec3(10.2f, 1.5f, 0.1f), wallColor, 0.8f, 0.0f);
    addCube("Wall_South", glm::vec3(0, 1.5f,  10.1f), glm::vec3(10.2f, 1.5f, 0.1f), wallColor, 0.8f, 0.0f);
    addCube("Wall_East",  glm::vec3(10.1f, 1.5f, 0),  glm::vec3(0.1f, 1.5f, 10.2f), wallColor, 0.8f, 0.0f);
    addCube("Wall_West",  glm::vec3(-10.1f, 1.5f, 0), glm::vec3(0.1f, 1.5f, 10.2f), wallColor, 0.8f, 0.0f);

    // Inner Walls
    addCube("Wall_Flur", glm::vec3(-5.0f, 1.5f, 2.0f), glm::vec3(0.1f, 1.5f, 8.0f), wallColor, 0.8f, 0.0f); 
    addCube("Wall_Flur_Top", glm::vec3(-7.5f, 1.5f, -6.0f), glm::vec3(2.5f, 1.5f, 0.1f), wallColor, 0.8f, 0.0f); 
    addCube("Wall_Kitchen", glm::vec3(2.5f, 1.5f, -1.0f), glm::vec3(7.5f, 1.5f, 0.1f), wallColor, 0.8f, 0.0f); 

    // --- Furniture & Objects ---

    // Living Room (Wohnzimmer) - Bottom Right
    // Living Room (Wohnzimmer) - Bottom Right
    // Blue Fabric Sofa (Armrests, Seat Cushion, Backrest, Silver Legs)
    addCube("Sofa_Seat", glm::vec3(2.0f, 0.35f, 8.0f), glm::vec3(1.6f, 0.15f, 0.8f), glm::vec3(0.24f, 0.44f, 0.69f), 0.85f, 0.0f);
    addCube("Sofa_Back", glm::vec3(2.0f, 0.7f, 8.4f), glm::vec3(1.6f, 0.3f, 0.12f), glm::vec3(0.24f, 0.44f, 0.69f), 0.85f, 0.0f);
    addCube("Sofa_Arm_L", glm::vec3(1.1f, 0.55f, 8.0f), glm::vec3(0.12f, 0.3f, 0.9f), glm::vec3(0.24f, 0.44f, 0.69f), 0.85f, 0.0f);
    addCube("Sofa_Arm_R", glm::vec3(2.9f, 0.55f, 8.0f), glm::vec3(0.12f, 0.3f, 0.9f), glm::vec3(0.24f, 0.44f, 0.69f), 0.85f, 0.0f);
    addCube("Sofa_Leg_FL", glm::vec3(1.15f, 0.1f, 7.65f), glm::vec3(0.04f, 0.1f, 0.04f), glm::vec3(0.7f, 0.7f, 0.7f), 0.3f, 0.8f);
    addCube("Sofa_Leg_FR", glm::vec3(2.85f, 0.1f, 7.65f), glm::vec3(0.04f, 0.1f, 0.04f), glm::vec3(0.7f, 0.7f, 0.7f), 0.3f, 0.8f);
    addCube("Sofa_Leg_BL", glm::vec3(1.15f, 0.1f, 8.35f), glm::vec3(0.04f, 0.1f, 0.04f), glm::vec3(0.7f, 0.7f, 0.7f), 0.3f, 0.8f);
    addCube("Sofa_Leg_BR", glm::vec3(2.85f, 0.1f, 8.35f), glm::vec3(0.04f, 0.1f, 0.04f), glm::vec3(0.7f, 0.7f, 0.7f), 0.3f, 0.8f);

    addCube("TV_Stand", glm::vec3(2.0f, 0.3f, 4.0f), glm::vec3(1.5f, 0.3f, 0.5f), glm::vec3(0.3f, 0.3f, 0.3f), 0.5f, 0.1f);
    
    // Retro TV (Wood Cabinet, Control Panel, Knobs, Tilted Metal Antennas)
    addCube("TV_Cabinet", glm::vec3(2.0f, 1.0f, 4.0f), glm::vec3(1.2f, 0.7f, 0.6f), glm::vec3(0.18f, 0.12f, 0.08f), 0.5f, 0.0f); // brown wood
    addCube("TV_Controls", glm::vec3(2.45f, 1.0f, 3.69f), glm::vec3(0.18f, 0.6f, 0.02f), glm::vec3(0.35f, 0.35f, 0.35f), 0.4f, 0.1f); // grey dials area
    addCube("TV_Knob1", glm::vec3(2.45f, 1.15f, 3.67f), glm::vec3(0.08f, 0.08f, 0.03f), glm::vec3(0.7f, 0.7f, 0.7f), 0.3f, 0.8f);
    addCube("TV_Knob2", glm::vec3(2.45f, 0.95f, 3.67f), glm::vec3(0.08f, 0.08f, 0.03f), glm::vec3(0.7f, 0.7f, 0.7f), 0.3f, 0.8f);
    addCube("TV_Bezel", glm::vec3(1.95f, 1.0f, 3.69f), glm::vec3(0.82f, 0.62f, 0.02f), glm::vec3(0.85f, 0.82f, 0.72f), 0.6f, 0.0f); // cream border
    
    // Add two tilted antennas
    SceneObject antL;
    antL.name = "TV_Antenna_L";
    antL.vao = Primitives::getCubeVAO();
    antL.vertexCount = 36;
    glm::mat4 antLMat = glm::mat4(1.0f);
    antLMat = glm::translate(antLMat, glm::vec3(1.85f, 1.55f, 4.0f));
    antLMat = glm::rotate(antLMat, glm::radians(25.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    antLMat = glm::scale(antLMat, glm::vec3(0.015f, 0.5f, 0.015f));
    antL.transform = antLMat;
    antL.color = glm::vec3(0.6f, 0.6f, 0.6f);
    antL.roughness = 0.2f;
    antL.metallic = 0.9f;
    objects.push_back(antL);
    
    SceneObject antR;
    antR.name = "TV_Antenna_R";
    antR.vao = Primitives::getCubeVAO();
    antR.vertexCount = 36;
    glm::mat4 antRMat = glm::mat4(1.0f);
    antRMat = glm::translate(antRMat, glm::vec3(2.15f, 1.55f, 4.0f));
    antRMat = glm::rotate(antRMat, glm::radians(-25.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    antRMat = glm::scale(antRMat, glm::vec3(0.015f, 0.5f, 0.015f));
    antR.transform = antRMat;
    antR.color = glm::vec3(0.6f, 0.6f, 0.6f);
    antR.roughness = 0.2f;
    antR.metallic = 0.9f;
    objects.push_back(antR);

    // Quad for the TV Screen
    SceneObject tvScreen;
    tvScreen.name = "TV_Screen";
    tvScreen.vao = Primitives::getQuadVAO();
    tvScreen.vertexCount = 4;
    tvScreen.drawAsQuad = true;
    glm::mat4 screenMat = glm::mat4(1.0f);
    screenMat = glm::translate(screenMat, glm::vec3(1.95f, 1.0f, 3.675f)); // in front of bezel
    screenMat = glm::scale(screenMat, glm::vec3(0.78f, 0.58f, 1.0f));
    tvScreen.transform = screenMat;
    tvScreen.color = glm::vec3(1.0f, 1.0f, 1.0f);
    tvScreen.roughness = 0.1f;
    tvScreen.metallic = 0.9f;
    objects.push_back(tvScreen);

    // Kitchen (Küche) - Top Right
    addCube("Kitchen_Counter", glm::vec3(2.0f, 0.5f, -5.0f), glm::vec3(2.0f, 0.5f, 1.0f), glm::vec3(0.8f, 0.8f, 0.8f), 0.4f, 0.1f);
    
    // Retro Fridge (Light/Pastel Blue: metallic 0.1, roughness 0.2)
    glm::vec3 fridgeBlue = glm::vec3(0.53f, 0.74f, 0.88f);
    addCube("Kuehlschrank", glm::vec3(-2.0f, 1.0f, -9.0f), glm::vec3(0.8f, 1.0f, 0.8f), fridgeBlue, 0.2f, 0.1f);
    addCube("Kuehlschrank_Tuer", glm::vec3(-2.0f, 1.0f, -8.1f), glm::vec3(0.8f, 1.0f, 0.1f), fridgeBlue, 0.2f, 0.1f);
    addCube("Kuehlschrank_Handle", glm::vec3(-1.62f, 1.2f, -8.02f), glm::vec3(0.02f, 0.04f, 0.2f), glm::vec3(0.85f, 0.85f, 0.85f), 0.1f, 0.95f);
    
    // Refrigerator interior (Shelves, can, bottle, milk)
    addCube("Fridge_Inside", glm::vec3(-2.0f, 1.0f, -9.1f), glm::vec3(0.7f, 0.9f, 0.1f), glm::vec3(0.95f, 0.95f, 0.95f), 0.2f, 0.1f);
    addCube("Fridge_Shelf1", glm::vec3(-2.0f, 1.2f, -8.7f), glm::vec3(0.7f, 0.02f, 0.5f), glm::vec3(0.9f, 0.9f, 0.95f), 0.1f, 0.0f);
    addCube("Fridge_Shelf2", glm::vec3(-2.0f, 0.7f, -8.7f), glm::vec3(0.7f, 0.02f, 0.5f), glm::vec3(0.9f, 0.9f, 0.95f), 0.1f, 0.0f);
    addCube("Fridge_Can1", glm::vec3(-1.8f, 1.3f, -8.7f), glm::vec3(0.1f, 0.15f, 0.1f), glm::vec3(0.85f, 0.1f, 0.1f), 0.3f, 0.2f);
    addCube("Fridge_Bottle1", glm::vec3(-2.2f, 1.35f, -8.7f), glm::vec3(0.08f, 0.25f, 0.08f), glm::vec3(0.1f, 0.6f, 0.15f), 0.1f, 0.0f);
    addCube("Fridge_Milk", glm::vec3(-2.0f, 0.85f, -8.7f), glm::vec3(0.12f, 0.22f, 0.12f), glm::vec3(0.9f, 0.9f, 0.9f), 0.6f, 0.0f);

    // Dining Room (Esszimmer) - Top Left
    addCube("Dining_Table", glm::vec3(-6.0f, 0.5f, -3.0f), glm::vec3(1.5f, 0.1f, 1.0f), glm::vec3(0.4f, 0.2f, 0.1f), 0.5f, 0.0f);
    addCube("Chair_1", glm::vec3(-6.0f, 0.25f, -1.5f), glm::vec3(0.3f, 0.25f, 0.3f), glm::vec3(0.2f, 0.2f, 0.2f), 0.7f, 0.0f);
    addCube("Chair_2", glm::vec3(-6.0f, 0.25f, -4.5f), glm::vec3(0.3f, 0.25f, 0.3f), glm::vec3(0.2f, 0.2f, 0.2f), 0.7f, 0.0f);

    // Hallway (Flur) - Left
    // Detailed open fuse box with switches and wire lines
    addCube("Sicherungskasten", glm::vec3(-9.9f, 1.5f, 5.0f), glm::vec3(0.1f, 0.4f, 0.3f), glm::vec3(0.4f, 0.4f, 0.4f), 0.3f, 0.8f);
    addCube("Sicherungskasten_Tuer", glm::vec3(-9.84f, 1.5f, 5.0f), glm::vec3(0.02f, 0.4f, 0.3f), glm::vec3(0.5f, 0.5f, 0.5f), 0.3f, 0.8f);
    
    // Internal Fuseboard wiring and components
    addCube("Fuse_Row1", glm::vec3(-9.92f, 1.6f, 5.0f), glm::vec3(0.04f, 0.06f, 0.24f), glm::vec3(0.75f, 0.75f, 0.75f), 0.4f, 0.1f);
    addCube("Fuse_Row2", glm::vec3(-9.92f, 1.4f, 5.0f), glm::vec3(0.04f, 0.06f, 0.24f), glm::vec3(0.75f, 0.75f, 0.75f), 0.4f, 0.1f);
    addCube("Fuse_Sw1", glm::vec3(-9.9f, 1.6f, 4.95f), glm::vec3(0.04f, 0.03f, 0.02f), glm::vec3(0.1f, 0.1f, 0.1f), 0.4f, 0.0f);
    addCube("Fuse_Sw2", glm::vec3(-9.9f, 1.6f, 5.00f), glm::vec3(0.04f, 0.03f, 0.02f), glm::vec3(0.1f, 0.4f, 0.9f), 0.4f, 0.0f);
    addCube("Fuse_Sw3", glm::vec3(-9.9f, 1.6f, 5.05f), glm::vec3(0.04f, 0.03f, 0.02f), glm::vec3(0.9f, 0.3f, 0.1f), 0.4f, 0.0f);
    addCube("Fuse_Sw4", glm::vec3(-9.9f, 1.4f, 4.95f), glm::vec3(0.04f, 0.03f, 0.02f), glm::vec3(0.1f, 0.1f, 0.1f), 0.4f, 0.0f);
    addCube("Fuse_Sw5", glm::vec3(-9.9f, 1.4f, 5.00f), glm::vec3(0.04f, 0.03f, 0.02f), glm::vec3(0.1f, 0.1f, 0.1f), 0.4f, 0.0f);
    addCube("Fuse_Sw6", glm::vec3(-9.9f, 1.4f, 5.05f), glm::vec3(0.04f, 0.03f, 0.02f), glm::vec3(0.1f, 0.4f, 0.9f), 0.4f, 0.0f);
    addCube("Fuse_Wire_B", glm::vec3(-9.94f, 1.5f, 4.90f), glm::vec3(0.01f, 0.36f, 0.01f), glm::vec3(0.1f, 0.3f, 0.9f), 0.7f, 0.0f);
    addCube("Fuse_Wire_YG", glm::vec3(-9.94f, 1.5f, 5.10f), glm::vec3(0.01f, 0.36f, 0.01f), glm::vec3(0.8f, 0.8f, 0.1f), 0.7f, 0.0f);
    addCube("Fuse_Wire_K", glm::vec3(-9.94f, 1.5f, 5.02f), glm::vec3(0.01f, 0.36f, 0.01f), glm::vec3(0.05f, 0.05f, 0.05f), 0.7f, 0.0f);
    addCube("Fuse_Wire_R", glm::vec3(-9.94f, 1.5f, 4.98f), glm::vec3(0.01f, 0.36f, 0.01f), glm::vec3(0.9f, 0.1f, 0.1f), 0.7f, 0.0f);

    addCube("Lichtschalter", glm::vec3(-5.1f, 1.2f, 8.0f), glm::vec3(0.05f, 0.1f, 0.1f), glm::vec3(0.9f, 0.9f, 0.9f), 0.3f, 0.1f);
    addCube("Haustuer", glm::vec3(-8.0f, 1.0f, 10.0f), glm::vec3(1.0f, 1.0f, 0.1f), glm::vec3(0.3f, 0.15f, 0.05f), 0.6f, 0.0f);

    // Shadows/Monsters
    addCube("Shadow_Monster", glm::vec3(-2.0f, 1.0f, -7.0f), glm::vec3(0.4f, 1.0f, 0.4f), glm::vec3(0.0f, 0.0f, 0.0f), 0.9f, 0.0f);  

    // Protagonist Kicking Leg & Shoe (hidden by default)
    addCube("Kicking_Leg", glm::vec3(0.0f, -10.0f, 0.0f), glm::vec3(0.12f, 0.12f, 0.8f), glm::vec3(0.1f, 0.15f, 0.3f), 0.7f, 0.0f);
    addCube("Kicking_Shoe", glm::vec3(0.0f, -10.0f, 0.0f), glm::vec3(0.14f, 0.14f, 0.2f), glm::vec3(0.02f, 0.02f, 0.02f), 0.5f, 0.1f);

    // Walking legs & shoes for first-person stride animation
    addCube("Walking_Leg_L", glm::vec3(0.0f, -10.0f, 0.0f), glm::vec3(0.1f, 0.1f, 0.6f), glm::vec3(0.1f, 0.15f, 0.3f), 0.7f, 0.0f);
    addCube("Walking_Shoe_L", glm::vec3(0.0f, -10.0f, 0.0f), glm::vec3(0.12f, 0.12f, 0.18f), glm::vec3(0.02f, 0.02f, 0.02f), 0.5f, 0.1f);
    addCube("Walking_Leg_R", glm::vec3(0.0f, -10.0f, 0.0f), glm::vec3(0.1f, 0.1f, 0.6f), glm::vec3(0.1f, 0.15f, 0.3f), 0.7f, 0.0f);
    addCube("Walking_Shoe_R", glm::vec3(0.0f, -10.0f, 0.0f), glm::vec3(0.12f, 0.12f, 0.18f), glm::vec3(0.02f, 0.02f, 0.02f), 0.5f, 0.1f);
}
