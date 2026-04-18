import tensorflow as tf

def convert_model_to_tflite(saved_model_path, tflite_path, use_quantization=False):
    """
    将训练好的 Keras SavedModel 转换为 TFLite 格式，支持量化。
    """
    print(f"Loading model from {saved_model_path}...")
    model = tf.keras.models.load_model(saved_model_path)
    
    print("Converting model to TFLite format...")
    converter = tf.lite.TFLiteConverter.from_keras_model(model)
    
    # 如果目标是在嵌入式设备 (如 LS2K0300) 上运行，启用量化可以提高推理速度且减少模型大小
    if use_quantization:
        print("Enabling default optimization (quantization)...")
        converter.optimizations = [tf.lite.Optimize.DEFAULT]
        
    tflite_model = converter.convert()
    
    with open(tflite_path, 'wb') as f:
        f.write(tflite_model)
    print(f"TFLite model successfully saved to: {tflite_path}")

if __name__ == "__main__":
    saved_model_dir = 'saved_models/mobilenet_v3_small_model.keras'
    tflite_file_path = 'saved_models/mobilenet_v3_small.tflite'
    
    # 转换为普通 TFLite 模型
    convert_model_to_tflite(saved_model_dir, tflite_file_path, use_quantization=False)
    
    # 转换为量化的 TFLite 模型
    tflite_quantized_path = 'saved_models/mobilenet_v3_small_quantized.tflite'
    convert_model_to_tflite(saved_model_dir, tflite_quantized_path, use_quantization=True)
