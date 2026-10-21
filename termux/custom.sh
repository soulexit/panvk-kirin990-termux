# Vulkan'a sadece bu özel ICD dosyasını okumasını söyleyin
export VK_ICD_FILENAMES=$PREFIX/share/vulkan/icd.d/panfrost_icd.aarch64.json

# OpenGL komutlarını Vulkan üzerinden yürütmek için Zink'i aktif edin
export GALLIUM_DRIVER=zink
export MESA_LOADER_DRIVER_NAME=panfrost

# Turnip sürücüleri kullanılıyorsa kararlılık için önerilen debug parametreleri
export TU_DEBUG=noconform
export ZINK_DESCRIPTORS=lazy
