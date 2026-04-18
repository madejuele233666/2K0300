import os
import tensorflow as tf
from tensorflow.keras.applications import MobileNetV3Small
from tensorflow.keras.layers import Dense, GlobalAveragePooling2D
from tensorflow.keras.models import Model
from tensorflow.keras.optimizers import Adam
import numpy as np

def build_mobilenetv3_model(num_classes=3, input_shape=(224, 224, 3)):
    """
    建立一个基于 MobileNetV3-Small 的迁移学习模型
    """
    # 导入预训练的基础模型，不包含顶部分类层
    base_model = MobileNetV3Small(
        weights='imagenet',
        include_top=False,
        input_shape=input_shape
    )
    
    # 冻结基础模型的层，为了初步训练新加的顶层
    base_model.trainable = False

    # 添加自定义的全连接层
    x = base_model.output
    x = GlobalAveragePooling2D()(x)
    x = Dense(128, activation='relu')(x)
    predictions = Dense(num_classes, activation='softmax')(x)

    model = Model(inputs=base_model.input, outputs=predictions)
    
    model.compile(
        optimizer=Adam(learning_rate=0.001),
        loss='sparse_categorical_crossentropy',
        metrics=['accuracy']
    )
    return model

if __name__ == "__main__":
    print("TensorFlow Version:", tf.__version__)
    
    # 参数设置
    num_classes = 3
    input_shape = (224, 224, 3)
    epochs = 10
    batch_size = 32
    dataset_dir = 'dataset'

    model = build_mobilenetv3_model(num_classes=num_classes, input_shape=input_shape)
    model.summary()

    # 从目录加载真实数据集
    print("\n--- Loading dataset from directory ---")
    train_dataset = tf.keras.utils.image_dataset_from_directory(
        dataset_dir,
        validation_split=0.2, # 80% 用于训练, 20% 用于验证
        subset="training",
        seed=123,
        image_size=(224, 224),
        batch_size=batch_size
    )

    val_dataset = tf.keras.utils.image_dataset_from_directory(
        dataset_dir,
        validation_split=0.2,
        subset="validation",
        seed=123,
        image_size=(224, 224),
        batch_size=batch_size
    )

    class_names = train_dataset.class_names
    print(f"Found classes: {class_names}")

    # 提高数据加载性能
    AUTOTUNE = tf.data.AUTOTUNE
    train_dataset = train_dataset.prefetch(buffer_size=AUTOTUNE)
    val_dataset = val_dataset.prefetch(buffer_size=AUTOTUNE)

    # 训练模型
    print("\n--- Starting model training ---")
    model.fit(
        train_dataset,
        validation_data=val_dataset,
        epochs=epochs
    )

    # 保存模型 (Keras format)
    os.makedirs('saved_models', exist_ok=True)
    model_path = 'saved_models/mobilenet_v3_small_model.keras'
    model.save(model_path)
    print(f"\nModel saved successfully at: {model_path}")
