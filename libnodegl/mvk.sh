export VULKAN_SDK="$(dirname $0)/vulkansdk-macos-1.1.77.0/MoltenVK/macOS"
export PATH=$PATH:"$(dirname $0)/vulkansdk-macos-1.1.77.0/macOS/bin"
export VK_ICD_FILENAMES=$VULKAN_SDK/etc/vulkan/icd.d/MoltenVK_icd.json
